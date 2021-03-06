#include "parser.h"
#include "value.h"
#include "op.h"
#include "vm.h"
#include "ht.h"
#include "vector.h"

typedef VECTOR_OF(Instr) Chunk;

typedef VECTOR_OF(size_t) FixupList;

typedef VECTOR_OF(FixupList) FixupStack;

static
void
fixup_stack_clear(FixupStack *fs)
{
    for (size_t i = 0; i < fs->size; ++i) {
        VECTOR_FREE(fs->data[i]);
    }
    VECTOR_CLEAR(*fs);
}

static
void
fixup_stack_free(FixupStack fs)
{
    for (size_t i = 0; i < fs.size; ++i) {
        VECTOR_FREE(fs.data[i]);
    }
    VECTOR_FREE(fs);
}

static inline
void
fixup_stack_last_push(FixupStack *fs, size_t fixup_pos)
{
    VECTOR_PUSH(fs->data[fs->size - 1], fixup_pos);
}

static
void
fixup_forward(Instr *chunk, FixupStack *fs, size_t pos)
{
    FixupList fl = VECTOR_POP(*fs);
    for (size_t i = 0; i < fl.size; ++i) {
        const size_t at = fl.data[i];
        chunk[at].args.offset = pos - at;
    }
    VECTOR_FREE(fl);
}

static
void
fixup_backward(Instr *chunk, FixupStack *fs, size_t pos)
{
    FixupList fl = VECTOR_POP(*fs);
    for (size_t i = 0; i < fl.size; ++i) {
        const size_t at = fl.data[i];
        chunk[at].args.offset = (ssize_t) pos - at - 1;
    }
    VECTOR_FREE(fl);
}

struct Parser {
    Lexer *lex;
    bool expr_end;
    Chunk chunk;
    Chunk aux_chunk;
    FixupStack fixup_cond;
    FixupStack fixup_loop_break;
    FixupStack fixup_loop_ctnue;
    VECTOR_OF(Ht *) locals;
    size_t bind_vars_from;
    unsigned line;
    jmp_buf err_handler;
    ParserError err;
};

Parser *
parser_new(Lexer *lex)
{
    Parser *p = XNEW(Parser, 1);
    *p = (Parser) {
        .lex = lex,
        .chunk = VECTOR_NEW(),
        .aux_chunk = VECTOR_NEW(),
        .fixup_cond = VECTOR_NEW(),
        .fixup_loop_break = VECTOR_NEW(),
        .fixup_loop_ctnue = VECTOR_NEW(),
        .locals = VECTOR_NEW(),
    };
    return p;
}

static
void
reset(Parser *p)
{
    p->expr_end = false;
    VECTOR_CLEAR(p->chunk);
    VECTOR_CLEAR(p->aux_chunk);
    fixup_stack_clear(&p->fixup_cond);
    fixup_stack_clear(&p->fixup_loop_break);
    fixup_stack_clear(&p->fixup_loop_ctnue);

    for (size_t i = 0; i < p->locals.size; ++i) {
        ht_destroy(p->locals.data[i]);
    }
    VECTOR_CLEAR(p->locals);

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

static ATTR_NORETURN
void
throw_at(Parser *p, Lexem pos, const char *msg)
{
    p->err = (ParserError) {.has_pos = true, .pos = pos, .msg = msg};
    longjmp(p->err_handler, 1);
}

static inline ATTR_NORETURN
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
        VECTOR_PUSH(p->chunk, ((Instr) {CMD_QUARK, {.nline = pos.line}}));
        p->line = pos.line;
    }
    VECTOR_PUSH(p->chunk, in);
}

static inline
void
emit_noquark(Parser *p, Instr in)
{
    VECTOR_PUSH(p->chunk, in);
}

static inline
void
emit_command_noquark(Parser *p, Command cmd)
{
    VECTOR_PUSH(p->chunk, (Instr) {.cmd = cmd});
}

static inline
void
After_expr(Parser *p, Lexem m)
{
    if (!p->expr_end) {
        throw_at(p, m, "expected expression");
    }
}

static inline
void
This_is_expr(Parser *p, Lexem m)
{
    if (p->expr_end) {
        throw_at(p, m, "expected operator or end of expression");
    }
}

static
Instr
assignment(Parser *p, const char *name, size_t nname, bool local)
{
    assert(p->locals.size);
    Ht *h = p->locals.data[p->locals.size - 1];
    if (local) {
        const unsigned index = ht_put(h, name, nname, ht_size(h));
        return (Instr) {CMD_STORE_FAST, {.index = index}};
    } else {
        const HtValue val = ht_get(h, name, nname);
        if (val != HT_NO_VALUE) {
            return (Instr) {CMD_STORE_FAST, {.index = val}};
        } else {
            return (Instr) {CMD_STORE, {.str = {name, nname}}};
        }
    }
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
            p->chunk.data[i] = (Instr) {CMD_LOAD_FAST, {.index = val}};
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
    VECTOR_PUSH(p->locals, h);

    emit_command_noquark(p, CMD_FUNCTION);
    return p->chunk.size - 1;
}

static
void
func_end(Parser *p, size_t fu_instr)
{
    bind_vars(p);

    Ht *h = VECTOR_POP(p->locals);
    const size_t nlocalstbl = ht_size(h);
    ht_destroy(h);

    emit_command_noquark(p, CMD_EXIT);

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
                This_is_expr(p, m);
                Scalar scalar;
                if (!scalar_parse(m.start, m.size, &scalar)) {
                    throw_at(p, m, "invalid number");
                }
                emit(p, m, (Instr) {CMD_LOAD_SCALAR, {.scalar = scalar}});
                p->expr_end = true;
            }
            break;

        case LEX_KIND_STR:
            {
                This_is_expr(p, m);
                emit(p, m, (Instr) {CMD_LOAD_STR, {.str = {m.start, m.size}}});
                p->expr_end = true;
            }
            break;

        case LEX_KIND_IDENT:
            {
                This_is_expr(p, m);
                emit(p, m, (Instr) {CMD_LOAD, {.str = {m.start, m.size}}});
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
                        After_expr(p, m);
                        emit(p, m, (Instr) {CMD_OP_UNARY, {.unary = op->exec.unary}});
                    } else {
                        This_is_expr(p, m);
                        StopTokenKind s = expr(p, op->priority);
                        emit(p, m, (Instr) {CMD_OP_UNARY, {.unary = op->exec.unary}});
                        if (s != STOP_TOK_OP) {
                            return s;
                        }
                    }
                } else {
                    After_expr(p, m);
                    p->expr_end = false;
                    StopTokenKind s = expr(p, op->priority + (op->assoc == OP_ASSOC_LEFT));
                    emit(p, m, (Instr) {CMD_OP_BINARY, {.binary = op->exec.binary}});
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
                emit(p, m, (Instr) {CMD_CALL, {.nargs = nargs}});
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
                emit(p, m, (Instr) {CMD_LOAD_AT, {.nindices = nindices}});
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
                emit(p, m, (Instr) {CMD_MATRIX, {.dims = {.height = height, .width = width}}});
            }
            break;

        case LEX_KIND_EOF:
            After_expr(p, m);
            return STOP_TOK_EOF;

        case LEX_KIND_RBRACE:
            After_expr(p, m);
            return STOP_TOK_RBRACE;

        case LEX_KIND_RBRACKET:
            After_expr(p, m);
            return STOP_TOK_RBRACKET;

        case LEX_KIND_COMMA:
            After_expr(p, m);
            p->expr_end = false;
            return STOP_TOK_COMMA;

        case LEX_KIND_SEMICOLON:
            After_expr(p, m);
            p->expr_end = false;
            return STOP_TOK_SEMICOLON;

        case LEX_KIND_ERROR:
            throw_at(p, m, m.data);
            break;

        case LEX_KIND_EQ:
            After_expr(p, m);
            p->expr_end = false;
            return STOP_TOK_EQ;

        case LEX_KIND_COLON_EQ:
            After_expr(p, m);
            p->expr_end = false;
            return STOP_TOK_COLON_EQ;

        case LEX_KIND_THEN:
            After_expr(p, m);
            p->expr_end = false;
            return STOP_TOK_THEN;

        case LEX_KIND_DO:
            After_expr(p, m);
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
            emit_command_noquark(p, CMD_JUMP);
            return end_of_stmt(p);
        }
        break;

    case LEX_KIND_CONTINUE:
        {
            if (!p->fixup_loop_ctnue.size) {
                throw_at(p, m, "'continue' outside of a cycle");
            }
            fixup_stack_last_push(&p->fixup_loop_ctnue, p->chunk.size);
            emit_command_noquark(p, CMD_JUMP);
            return end_of_stmt(p);
        }
        break;

    case LEX_KIND_IF:
        {
            if (expr(p, -1) != STOP_TOK_THEN) {
                throw_there(p, "expected 'then'");
            }

            VECTOR_PUSH(p->fixup_cond, (FixupList) VECTOR_NEW());

            size_t prev_jump_unless = p->chunk.size;
            emit_command_noquark(p, CMD_JUMP_UNLESS);

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
                    emit_command_noquark(p, CMD_JUMP);

                    p->chunk.data[prev_jump_unless].args.offset = p->chunk.size - prev_jump_unless;

                    if (expr(p, -1) != STOP_TOK_THEN) {
                        throw_there(p, "expected 'then'");
                    }
                    prev_jump_unless = p->chunk.size;
                    emit_command_noquark(p, CMD_JUMP_UNLESS);

                } else if (s == STOP_TOK_ELSE) {
                    if (else_seen) {
                        throw_there(p, "double 'else'");
                    }

                    fixup_stack_last_push(&p->fixup_cond, p->chunk.size);
                    emit_command_noquark(p, CMD_JUMP);

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

            VECTOR_PUSH(p->fixup_loop_break, (FixupList) VECTOR_NEW());
            VECTOR_PUSH(p->fixup_loop_ctnue,  (FixupList) VECTOR_NEW());

            if (expr(p, -1) != STOP_TOK_DO) {
                throw_there(p, "expected 'do'");
            }

            const size_t jump_instr = p->chunk.size;
            emit_command_noquark(p, CMD_JUMP_UNLESS);

            StopTokenKind s;
            while ((s = stmt(p)) == STOP_TOK_SEMICOLON) {}
            if (s != STOP_TOK_END) {
                throw_there(p, "expected 'end'");
            }

            emit_noquark(p, (Instr) {
                CMD_JUMP,
                {.offset = (ssize_t) check_instr - p->chunk.size + 1}
            });

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

            VECTOR_PUSH(p->fixup_loop_break, (FixupList) VECTOR_NEW());
            VECTOR_PUSH(p->fixup_loop_ctnue,  (FixupList) VECTOR_NEW());

            // initial value
            if (expr(p, -1) != STOP_TOK_SEMICOLON) {
                throw_there(p, "expected ';'");
            }
            emit_noquark(p, assignment(p, var.start, var.size, true));

            // loop condition
            const size_t check_instr = p->chunk.size;
            if (expr(p, -1) != STOP_TOK_SEMICOLON) {
                throw_there(p, "expected ';'");
            }

            const size_t jump_instr = p->chunk.size;
            emit_command_noquark(p, CMD_JUMP_UNLESS);

            // assignment
            p->line = 0;
            const size_t old_aux_size = p->aux_chunk.size;
            swap_chunks(p);
            if (expr(p, -1) != STOP_TOK_DO) {
                throw_there(p, "expected 'do'");
            }
            emit_noquark(p, assignment(p, var.start, var.size, true));
            swap_chunks(p);

            // loop body
            StopTokenKind s;
            while ((s = stmt(p)) == STOP_TOK_SEMICOLON) {}
            if (s != STOP_TOK_END) {
                throw_there(p, "expected 'end'");
            }

            const size_t cont_instr = p->chunk.size;

            // TODO more efficient implementation
            for (size_t i = old_aux_size; i < p->aux_chunk.size; ++i) {
                VECTOR_PUSH(p->chunk, p->aux_chunk.data[i]);
            }
            p->aux_chunk.size = old_aux_size;

            emit_noquark(p, (Instr) {
                CMD_JUMP,
                {.offset = (ssize_t) check_instr - p->chunk.size + 1}
            });

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
            emit_command_noquark(p, CMD_EXIT);
            return end_of_stmt(p);
        }
        break;

    case LEX_KIND_RETURN:
        {
            StopTokenKind s = expr(p, -1);
            emit_command_noquark(p, CMD_RETURN);
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
            emit_noquark(p, assignment(p, funame.start, funame.size, false));
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
                emit_command_noquark(p, CMD_PRINT);
                return s;
            case STOP_TOK_EQ:
            case STOP_TOK_COLON_EQ:
                {
                    Instr last = VECTOR_POP(p->chunk);
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
                    StopTokenKind s2 = expr(p, -1);
                    emit_noquark(p, last);
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

    StopTokenKind s;
    while ((s = stmt(p)) == STOP_TOK_SEMICOLON) {}
    if (s != STOP_TOK_EOF) {
        throw_there(p, "syntax error");
    }

    func_end(p, fu_instr);

    emit_command_noquark(p, CMD_CALL);
    emit_command_noquark(p, CMD_PRINT);
    emit_command_noquark(p, CMD_EXIT);

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
    VECTOR_FREE(p->chunk);
    VECTOR_FREE(p->aux_chunk);
    fixup_stack_free(p->fixup_cond);
    fixup_stack_free(p->fixup_loop_break);
    fixup_stack_free(p->fixup_loop_ctnue);

    for (size_t i = 0; i < p->locals.size; ++i) {
        ht_destroy(p->locals.data[i]);
    }
    VECTOR_FREE(p->locals);

    free(p);
}
