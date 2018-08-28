#include "parser.h"
#include <setjmp.h>

#include "libls/vector.h"

#include "value.h"
#include "op.h"
#include "vm.h"
#include "ht.h"

typedef LS_VECTOR_OF(Instr) Chunk;

typedef LS_VECTOR_OF(size_t) FixupList;

typedef LS_VECTOR_OF(FixupList) FixupStack;

static
void
fixup_stack_clear(FixupStack *fs)
{
    for (size_t i = 0; i < fs->size; ++i) {
        LS_VECTOR_FREE(fs->data[i]);
    }
    LS_VECTOR_CLEAR(*fs);
}

static
void
fixup_stack_free(FixupStack fs)
{
    fixup_stack_clear(&fs);
    LS_VECTOR_FREE(fs);
}

static inline
void
fixup_stack_last_push(FixupStack *fs, size_t fixup_pos)
{
    LS_VECTOR_PUSH(fs->data[fs->size - 1], fixup_pos);
}

static
void
fixup_forward(Instr *chunk, FixupStack *fs, size_t pos)
{
    FixupList fl = fs->data[fs->size - 1];
    for (size_t i = 0; i < fl.size; ++i) {
        const size_t at = fl.data[i];
        chunk[at].args.offset = pos - at;
    }
    LS_VECTOR_FREE(fl);
    --fs->size;
}

static
void
fixup_backward(Instr *chunk, FixupStack *fs, size_t pos)
{
    FixupList fl = fs->data[fs->size - 1];
    for (size_t i = 0; i < fl.size; ++i) {
        const size_t at = fl.data[i];
        chunk[at].args.offset = (ssize_t) pos - at - 1;
    }
    LS_VECTOR_FREE(fl);
    --fs->size;
}

struct Parser {
    Lexer *lex;
    bool expr_end;
    Chunk chunk;
    Chunk aux_chunk;
    FixupStack fixup_cond;
    FixupStack fixup_loop_break;
    FixupStack fixup_loop_ctnue;
    LS_VECTOR_OF(Ht *) locals;
    size_t bind_vars_from;
    unsigned line;
    jmp_buf err_handler;
    ParserError err;
};

Parser *
parser_new(Lexer *lex)
{
    Parser *p = LS_XNEW(Parser, 1);
    *p = (Parser) {
        .lex = lex,
        .chunk = LS_VECTOR_NEW(),
        .aux_chunk = LS_VECTOR_NEW(),
        .fixup_cond = LS_VECTOR_NEW(),
        .fixup_loop_break = LS_VECTOR_NEW(),
        .fixup_loop_ctnue = LS_VECTOR_NEW(),
        .locals = LS_VECTOR_NEW(),
    };
    return p;
}

static
void
reset(Parser *p)
{
    p->expr_end = false;
    LS_VECTOR_CLEAR(p->chunk);
    LS_VECTOR_CLEAR(p->aux_chunk);
    fixup_stack_clear(&p->fixup_cond);
    fixup_stack_clear(&p->fixup_loop_break);
    fixup_stack_clear(&p->fixup_loop_ctnue);

    for (size_t i = 0; i < p->locals.size; ++i) {
        ht_destroy(p->locals.data[i]);
    }
    LS_VECTOR_CLEAR(p->locals);

    p->bind_vars_from = 0;
    p->line = 0;
}

typedef enum {
    STOP_TOK_OP,
    STOP_TOK_RBRACE,
    STOP_TOK_RBRACKET,
    STOP_TOK_COMMA,
    STOP_TOK_SEMICOLON,
    STOP_TOK_EQ,
    STOP_TOK_COLON_EQ,

    STOP_TOK_NONSENSE,

    STOP_TOK_THEN,
    STOP_TOK_DO,

    STOP_TOK_EOF,

    // for stmt() only:
    STOP_TOK_ELIF,
    STOP_TOK_ELSE,
    STOP_TOK_END,
} StopTokenKind;

static LS_ATTR_NORETURN
void
throw_at(Parser *p, Lexem pos, const char *msg)
{
    p->err = (ParserError) {.has_pos = true, .pos = pos, .msg = msg};
    longjmp(p->err_handler, 1);
}

static inline LS_ATTR_NORETURN
void
throw_there(Parser *p, const char *msg)
{
    lexer_rollback(p->lex);
    throw_at(p, lexer_next(p->lex), msg);
}

static inline
void
emit(Parser *p, Lexem pos, Instr in)
{
    if (pos.line != p->line) {
        LS_VECTOR_PUSH(p->chunk, ((Instr) {.cmd = CMD_QUARK, .args = {.nline = pos.line}}));
        p->line = pos.line;
    }
    LS_VECTOR_PUSH(p->chunk, in);
}

#define INSTR(P_, M_, Cmd_, ...) \
    emit(P_, M_, (Instr) {.cmd = Cmd_, .args = {__VA_ARGS__}})

#define INSTR_1(P_, Cmd_, ...) \
    LS_VECTOR_PUSH((P_)->chunk, ((Instr) {.cmd = Cmd_, .args = {__VA_ARGS__}}))

#define INSTR_1N(P_, Cmd_) \
    LS_VECTOR_PUSH((P_)->chunk, ((Instr) {.cmd = Cmd_}))

#define AFTER_EXPR(P_, M_) \
    do { \
        if (!(P_)->expr_end) { \
            throw_at(P_, M_, "expected expression"); \
        } \
    } while (0)

#define THIS_IS_EXPR(P_, M_) \
    do { \
        if ((P_)->expr_end) { \
            throw_at(P_, M_, "expected operator or end of expression"); \
        } \
    } while (0)

static
Instr
assignment(Parser *p, const char *name, size_t nname, bool local)
{
    assert(p->locals.size);
    Ht *h = p->locals.data[p->locals.size - 1];
    if (local) {
        const unsigned index = ht_put(h, name, nname, ht_size(h));
        return (Instr) {
            .cmd = CMD_STORE_FAST,
            .args = {.index = index},
        };
    } else {
        const HtValue val = ht_get(h, name, nname);
        if (val != HT_NO_VALUE) {
            return (Instr) {
                .cmd = CMD_STORE_FAST,
                .args = {.index = val},
            };
        }
    }
    return (Instr) {
        .cmd = CMD_STORE,
        .args = {.str = {
            .start = name,
            .size = nname,
        }},
    };
}

static
void
bind_vars(Parser *p)
{
    const size_t nchunk = p->chunk.size;
    if (!nchunk) {
        return;
    }
    assert(p->locals.size);
    Ht *h = p->locals.data[p->locals.size - 1];

    for (size_t i = p->bind_vars_from; i < nchunk; ++i) {
        const Instr in = p->chunk.data[i];
        if (in.cmd != CMD_LOAD) {
            continue;
        }
        const HtValue val = ht_get(h, in.args.str.start, in.args.str.size);
        if (val != HT_NO_VALUE) {
            p->chunk.data[i] = (Instr) {
                .cmd = CMD_LOAD_FAST,
                .args = {.index = val},
            };
        }
    }

    p->bind_vars_from = nchunk;
}

// forward declaration
static inline
StopTokenKind
expr(Parser *p, int min_priority);

static inline
bool
row(Parser *p, unsigned *width)
{
    *width = 1;
    while (1) {
        switch (expr(p, -1)) {
        case STOP_TOK_COMMA:
            ++*width;
            break;
        case STOP_TOK_SEMICOLON:
            return false;
        case STOP_TOK_RBRACKET:
            return true;
        default:
            throw_there(p, "expected either ',' or ';' or ']'");
        }
    }
}

static
size_t
func_begin(Parser *p)
{
    if (p->chunk.size) {
        bind_vars(p);
    }

    Ht *h = ht_new(2);
    LS_VECTOR_PUSH(p->locals, h);

    INSTR_1N(p, CMD_FUNCTION);
    return p->chunk.size - 1;
}

static
void
func_end(Parser *p, size_t fu_instr)
{
    bind_vars(p);

    Ht *h = p->locals.data[--p->locals.size];
    const size_t nlocalstbl = ht_size(h);
    ht_destroy(h);

    INSTR_1N(p, CMD_EXIT);

    Instr *fu = &p->chunk.data[fu_instr];
    fu->args.func.offset = p->chunk.size - fu_instr;
    fu->args.func.nlocals = nlocalstbl - fu->args.func.nargs;
}

static
size_t
paramlist(Parser *p, LexemKind terminator)
{
    const size_t fu_instr = func_begin(p);

    Ht *h = p->locals.data[p->locals.size - 1];

    unsigned nargs = 0;
    bool ident_expected = false;
    while (1) {
        Lexem m = lexer_next(p->lex);
        if (m.kind == LEX_KIND_IDENT) {
            if (!ident_expected && nargs != 0) {
                throw_at(p, m, "expected ',' or end of parameter list");
            }
            ht_put(h, m.start, m.size, nargs);
            ident_expected = false;
            ++nargs;
            if (nargs > VM_MAX_NARGS) {
                throw_at(p, m, "too many parameters");
            }
        } else if (m.kind == LEX_KIND_COMMA) {
            if (nargs == 0) {
                throw_at(p, m, "expected parameter name or end of parameter list");
            }
            ident_expected = true;
        } else if (m.kind == terminator) {
            if (ident_expected) {
                throw_at(p, m, "expected parameter name");
            }
            break;
        } else {
            throw_at(p, m, "expected parameter list");
        }
    }
    p->chunk.data[fu_instr].args.func.nargs = nargs;
    return fu_instr;
}

static inline
StopTokenKind
expr(Parser *p, int min_priority)
{
    while (1) {
        lexer_mark(p->lex);
        Lexem m = lexer_next(p->lex);

        switch (m.kind) {

        case LEX_KIND_NUM:
            {
                THIS_IS_EXPR(p, m);
                Scalar scalar;
                if (!scalar_parse(m.start, m.size, &scalar)) {
                    throw_at(p, m, "invalid number");
                }
                INSTR(p, m, CMD_LOAD_SCALAR, .scalar = scalar);
                p->expr_end = true;
            }
            break;

        case LEX_KIND_STR:
            {
                THIS_IS_EXPR(p, m);
                INSTR(p, m, CMD_LOAD_STR, .str = {.start = m.start, .size = m.size});
                p->expr_end = true;
            }
            break;

        case LEX_KIND_IDENT:
            {
                THIS_IS_EXPR(p, m);
                INSTR(p, m, CMD_LOAD, .str = {.start = m.start, .size = m.size});
                p->expr_end = true;
            }
            break;

        case LEX_KIND_AMBIG_OP:
            {
                AmbigOp *amb = m.data;
                m.kind = LEX_KIND_OP;
                m.data = p->expr_end ? amb->infix : amb->prefix;
            }
            // fallthrough
        case LEX_KIND_OP:
            {
                Op *op = m.data;

                if ((int) op->priority < min_priority &&
                    !(op->arity == 1 && op->assoc == OP_ASSOC_RIGHT))
                {
                    lexer_rollback(p->lex);
                    return STOP_TOK_OP;
                }

                if (op->arity == 1) {
                    if (op->assoc == OP_ASSOC_LEFT) {
                        AFTER_EXPR(p, m);
                        INSTR(p, m, CMD_OP_UNARY, .unary = op->exec.unary);
                    } else {
                        THIS_IS_EXPR(p, m);
                        StopTokenKind s = expr(p, op->priority);
                        INSTR(p, m, CMD_OP_UNARY, .unary = op->exec.unary);
                        if (s != STOP_TOK_OP) {
                            return s;
                        }
                    }
                } else {
                    AFTER_EXPR(p, m);
                    p->expr_end = false;
                    StopTokenKind s = expr(p, op->priority + (op->assoc == OP_ASSOC_LEFT));
                    INSTR(p, m, CMD_OP_BINARY, .binary = op->exec.binary);
                    if (s != STOP_TOK_OP) {
                        return s;
                    }
                }
            }
            break;

        case LEX_KIND_LBRACE:
            if (p->expr_end) {
                // function application
                lexer_mark(p->lex);
                unsigned nargs;
                if (lexer_next(p->lex).kind == LEX_KIND_RBRACE) {
                    nargs = 0;
                } else {
                    nargs = 1;
                    lexer_rollback(p->lex);
                    p->expr_end = false;
                    while (1) {
                        StopTokenKind s = expr(p, -1);
                        if (s == STOP_TOK_RBRACE) {
                            break;
                        } else if (s == STOP_TOK_COMMA) {
                            ++nargs;
                        } else {
                            throw_there(p, "expected either ',' or ')'");
                        }
                    }
                }
                INSTR(p, m, CMD_CALL, .nargs = nargs);
            } else {
                // grouping
                if (expr(p, -1) != STOP_TOK_RBRACE) {
                    throw_there(p, "expected ')'");
                }
            }
            break;

        case LEX_KIND_LBRACKET:
            if (p->expr_end) {
                // indexing
                unsigned nindices = 1;
                p->expr_end = false;
                while (1) {
                    StopTokenKind s = expr(p, -1);
                    if (s == STOP_TOK_RBRACKET) {
                        break;
                    } else if (s == STOP_TOK_COMMA) {
                        ++nindices;
                    } else {
                        throw_there(p, "expected either ',' or ']'");
                    }
                }
                INSTR(p, m, CMD_LOAD_AT, .nindices = nindices);
            } else {
                // matrix
                unsigned width;
                unsigned height;

                lexer_mark(p->lex);
                if (lexer_next(p->lex).kind == LEX_KIND_RBRACKET) {
                    width = height = 0;
                    p->expr_end = true;
                } else {
                    lexer_rollback(p->lex);
                    p->expr_end = false;
                    height = 1;
                    for (bool done = row(p, &width); !done; ++height) {
                        unsigned cur_width;
                        done = row(p, &cur_width);
                        if (cur_width != width) {
                            throw_there(p, "wrong row length");
                        }
                    }
                }
                INSTR(p, m, CMD_MATRIX, .dims = {.height = height, .width = width});
            }
            break;

        case LEX_KIND_EOF:
            AFTER_EXPR(p, m);
            return STOP_TOK_EOF;

        case LEX_KIND_RBRACE:
            AFTER_EXPR(p, m);
            return STOP_TOK_RBRACE;

        case LEX_KIND_RBRACKET:
            AFTER_EXPR(p, m);
            return STOP_TOK_RBRACKET;

        case LEX_KIND_COMMA:
            AFTER_EXPR(p, m);
            p->expr_end = false;
            return STOP_TOK_COMMA;

        case LEX_KIND_SEMICOLON:
            AFTER_EXPR(p, m);
            p->expr_end = false;
            return STOP_TOK_SEMICOLON;

        case LEX_KIND_ERROR:
            throw_at(p, m, m.data);
            break;

        case LEX_KIND_EQ:
            AFTER_EXPR(p, m);
            p->expr_end = false;
            return STOP_TOK_EQ;

        case LEX_KIND_COLON_EQ:
            AFTER_EXPR(p, m);
            p->expr_end = false;
            return STOP_TOK_COLON_EQ;

        case LEX_KIND_THEN:
            AFTER_EXPR(p, m);
            p->expr_end = false;
            return STOP_TOK_THEN;

        case LEX_KIND_DO:
            AFTER_EXPR(p, m);
            p->expr_end = false;
            return STOP_TOK_DO;

        case LEX_KIND_IF:
        case LEX_KIND_ELIF:
        case LEX_KIND_ELSE:
        case LEX_KIND_WHILE:
        case LEX_KIND_BREAK:
        case LEX_KIND_CONTINUE:
        case LEX_KIND_FU:
        case LEX_KIND_RETURN:
        case LEX_KIND_EXIT:
        case LEX_KIND_END:
        case LEX_KIND_FOR:
        case LEX_KIND_BAR:
            return STOP_TOK_NONSENSE;
        }
    }
}

static
void
swap_chunks(Parser *p)
{
    Chunk tmp = p->chunk;
    p->chunk = p->aux_chunk;
    p->aux_chunk = tmp;
}

static inline
StopTokenKind
end_of_stmt(Parser *p)
{
    Lexem m = lexer_next(p->lex);
    switch (m.kind) {
    case LEX_KIND_SEMICOLON:
        return STOP_TOK_SEMICOLON;
    case LEX_KIND_EOF:
        return STOP_TOK_EOF;
    default:
        throw_at(p, m, "expected end of statement");
    }
}

StopTokenKind
stmt(Parser *p)
{
    lexer_mark(p->lex);
    Lexem m = lexer_next(p->lex);
    switch (m.kind) {
    case LEX_KIND_SEMICOLON:
        return STOP_TOK_SEMICOLON;

    case LEX_KIND_EOF:
        return STOP_TOK_EOF;

    case LEX_KIND_ELIF:
        return STOP_TOK_ELIF;

    case LEX_KIND_ELSE:
        return STOP_TOK_ELSE;

    case LEX_KIND_END:
        return STOP_TOK_END;

    case LEX_KIND_BREAK:
        {
            if (!p->fixup_loop_break.size) {
                throw_at(p, m, "'break' outside of a cycle");
            }
            fixup_stack_last_push(&p->fixup_loop_break, p->chunk.size);
            INSTR_1N(p, CMD_JUMP);
            return end_of_stmt(p);
        }
        break;

    case LEX_KIND_CONTINUE:
        {
            if (!p->fixup_loop_ctnue.size) {
                throw_at(p, m, "'continue' outside of a cycle");
            }
            fixup_stack_last_push(&p->fixup_loop_ctnue, p->chunk.size);
            INSTR_1N(p, CMD_JUMP);
            return end_of_stmt(p);
        }
        break;

    case LEX_KIND_IF:
        {
            if (expr(p, -1) != STOP_TOK_THEN) {
                throw_there(p, "expected 'then'");
            }

            LS_VECTOR_PUSH(p->fixup_cond, (FixupList) LS_VECTOR_NEW());

            size_t prev_jump_unless = p->chunk.size;
            INSTR_1N(p, CMD_JUMP_UNLESS);

            bool else_seen = false;
            while (1) {
                StopTokenKind s;
                while ((s = stmt(p)) == STOP_TOK_SEMICOLON) {}
                if (s == STOP_TOK_END) {
                    break;

                } else if (s == STOP_TOK_ELIF) {
                    if (else_seen) {
                        throw_there(p, "'elif' after 'else'");
                    }

                    fixup_stack_last_push(&p->fixup_cond, p->chunk.size);
                    INSTR_1N(p, CMD_JUMP);

                    p->chunk.data[prev_jump_unless].args.offset = p->chunk.size - prev_jump_unless;

                    if (expr(p, -1) != STOP_TOK_THEN) {
                        throw_there(p, "expected 'then'");
                    }
                    prev_jump_unless = p->chunk.size;
                    INSTR_1N(p, CMD_JUMP_UNLESS);

                } else if (s == STOP_TOK_ELSE) {
                    if (else_seen) {
                        throw_there(p, "double 'else'");
                    }

                    fixup_stack_last_push(&p->fixup_cond, p->chunk.size);
                    INSTR_1N(p, CMD_JUMP);

                    p->chunk.data[prev_jump_unless].args.offset = p->chunk.size - prev_jump_unless;

                    prev_jump_unless = (size_t) -1;

                    else_seen = true;

                } else {
                    throw_there(p, "expected 'elif', 'else' or 'end'");
                }
            }

            const size_t end_pos = p->chunk.size;
            if (prev_jump_unless != (size_t) -1) {
                p->chunk.data[prev_jump_unless].args.offset = end_pos - prev_jump_unless;
            }

            fixup_forward(p->chunk.data, &p->fixup_cond, end_pos);

            p->expr_end = false;

            return end_of_stmt(p);
        }
        break;

    case LEX_KIND_WHILE:
        {
            const size_t check_instr = p->chunk.size;

            LS_VECTOR_PUSH(p->fixup_loop_break, (FixupList) LS_VECTOR_NEW());
            LS_VECTOR_PUSH(p->fixup_loop_ctnue,  (FixupList) LS_VECTOR_NEW());

            if (expr(p, -1) != STOP_TOK_DO) {
                throw_there(p, "expected 'do'");
            }

            const size_t jump_instr = p->chunk.size;
            INSTR_1N(p, CMD_JUMP_UNLESS);

            StopTokenKind s;
            while ((s = stmt(p)) == STOP_TOK_SEMICOLON) {}
            if (s != STOP_TOK_END) {
                throw_there(p, "expected 'end'");
            }

            INSTR_1(p, CMD_JUMP, .offset = (ssize_t) check_instr - p->chunk.size + 1);

            const size_t end_pos = p->chunk.size;
            p->chunk.data[jump_instr].args.offset = end_pos - jump_instr;

            fixup_forward(p->chunk.data, &p->fixup_loop_break, end_pos);
            fixup_backward(p->chunk.data, &p->fixup_loop_ctnue, jump_instr);

            p->expr_end = false;

            return end_of_stmt(p);
        }
        break;

    case LEX_KIND_FOR:
        {
            Lexem var = lexer_next(p->lex);
            if (var.kind != LEX_KIND_IDENT) {
                throw_at(p, var, "expected identifier");
            }

            Lexem bar = lexer_next(p->lex);
            if (bar.kind != LEX_KIND_BAR) {
                throw_at(p, bar, "expected '|'");
            }

            LS_VECTOR_PUSH(p->fixup_loop_break, (FixupList) LS_VECTOR_NEW());
            LS_VECTOR_PUSH(p->fixup_loop_ctnue,  (FixupList) LS_VECTOR_NEW());

            // initial value
            if (expr(p, -1) != STOP_TOK_SEMICOLON) {
                throw_there(p, "expected ';'");
            }
            LS_VECTOR_PUSH(p->chunk, assignment(p, var.start, var.size, true));

            // loop condition
            const size_t check_instr = p->chunk.size;
            if (expr(p, -1) != STOP_TOK_SEMICOLON) {
                throw_there(p, "expected ';'");
            }

            const size_t jump_instr = p->chunk.size;
            INSTR_1N(p, CMD_JUMP_UNLESS);

            // assignment
            p->line = 0;
            const size_t old_aux_size = p->aux_chunk.size;
            swap_chunks(p);
            if (expr(p, -1) != STOP_TOK_DO) {
                throw_there(p, "expected 'do'");
            }
            LS_VECTOR_PUSH(p->chunk, assignment(p, var.start, var.size, true));
            swap_chunks(p);

            // loop body
            StopTokenKind s;
            while ((s = stmt(p)) == STOP_TOK_SEMICOLON) {}
            if (s != STOP_TOK_END) {
                throw_there(p, "expected 'end'");
            }

            const size_t cont_instr = p->chunk.size;

            for (size_t i = old_aux_size; i < p->aux_chunk.size; ++i) {
                LS_VECTOR_PUSH(p->chunk, p->aux_chunk.data[i]);
            }
            p->aux_chunk.size = old_aux_size;

            INSTR_1(p, CMD_JUMP, .offset = (ssize_t) check_instr - p->chunk.size + 1);

            const size_t end_pos = p->chunk.size;
            p->chunk.data[jump_instr].args.offset = end_pos - jump_instr;

            fixup_forward(p->chunk.data, &p->fixup_loop_break, end_pos);
            fixup_forward(p->chunk.data, &p->fixup_loop_ctnue, cont_instr);

            p->expr_end = false;

            return end_of_stmt(p);
        }
        break;


    case LEX_KIND_EXIT:
        {
            INSTR_1N(p, CMD_EXIT);
            return end_of_stmt(p);
        }
        break;

    case LEX_KIND_RETURN:
        {
            StopTokenKind s = expr(p, -1);
            INSTR_1N(p, CMD_RETURN);
            switch (s) {
            case STOP_TOK_SEMICOLON:
                return STOP_TOK_SEMICOLON;
            case STOP_TOK_EOF:
                return STOP_TOK_EOF;
            default:
                throw_there(p, "expected end of expression");
            }
        }
        break;

    case LEX_KIND_FU:
        {
            Lexem funame = lexer_next(p->lex);
            if (funame.kind != LEX_KIND_IDENT) {
                throw_at(p, funame, "expected identifier");
            }

            Lexem lbrace = lexer_next(p->lex);
            if (lbrace.kind != LEX_KIND_LBRACE) {
                throw_at(p, lbrace, "expected '('");
            }

            const size_t fu_instr = paramlist(p, LEX_KIND_RBRACE);

            StopTokenKind s;
            while ((s = stmt(p)) == STOP_TOK_SEMICOLON) {}
            if (s != STOP_TOK_END) {
                throw_there(p, "expected 'end'");
            }

            func_end(p, fu_instr);
            LS_VECTOR_PUSH(p->chunk, assignment(p, funame.start, funame.size, false));
            return end_of_stmt(p);
        }
        break;

    default:
        {
            lexer_rollback(p->lex);
            StopTokenKind s = expr(p, -1);
            switch (s) {
            case STOP_TOK_SEMICOLON:
            case STOP_TOK_EOF:
                INSTR_1N(p, CMD_PRINT);
                return s;
            case STOP_TOK_EQ:
            case STOP_TOK_COLON_EQ:
                {
                    Instr last = p->chunk.data[p->chunk.size - 1];
                    switch (last.cmd) {
                    case CMD_LOAD:
                        last = assignment(
                            p,
                            last.args.str.start,
                            last.args.str.size,
                            s == STOP_TOK_COLON_EQ);
                        break;
                    case CMD_LOAD_AT:
                        if (s == STOP_TOK_EQ) {
                            last.cmd = CMD_STORE_AT;
                            break;
                        }
                        // fallthrough
                    default:
                        throw_there(p, "invalid assignment");
                    }
                    --p->chunk.size;
                    StopTokenKind s2 = expr(p, -1);
                    LS_VECTOR_PUSH(p->chunk, last);
                    switch (s2) {
                    case STOP_TOK_SEMICOLON:
                    case STOP_TOK_EOF:
                        return s2;
                    default:
                        throw_there(p, "syntax error");
                    }
                }
                break;
            default:
                throw_there(p, "syntax error");
            }
        }
    }
}

bool
parser_parse(Parser *p)
{
    reset(p);

    if (setjmp(p->err_handler) != 0) {
        return false;
    }

    const size_t fu_instr = func_begin(p);

    StopTokenKind s2;
    while ((s2 = stmt(p)) == STOP_TOK_SEMICOLON) {}
    if (s2 != STOP_TOK_EOF) {
        throw_there(p, "syntax error");
    }

    func_end(p, fu_instr);

    INSTR_1N(p, CMD_CALL);
    INSTR_1N(p, CMD_PRINT);
    INSTR_1N(p, CMD_EXIT);

    return true;
}

const Instr *
parser_last_chunk(Parser *p, size_t *nchunk)
{
    *nchunk = p->chunk.size;
    return p->chunk.data;
}

ParserError
parser_last_error(Parser *p)
{
    return p->err;
}

void
parser_destroy(Parser *p)
{
    LS_VECTOR_FREE(p->chunk);
    LS_VECTOR_FREE(p->aux_chunk);
    fixup_stack_free(p->fixup_cond);
    fixup_stack_free(p->fixup_loop_break);
    fixup_stack_free(p->fixup_loop_ctnue);

    for (size_t i = 0; i < p->locals.size; ++i) {
        ht_destroy(p->locals.data[i]);
    }
    LS_VECTOR_FREE(p->locals);

    free(p);
}
