#include "parser.h"
#include <setjmp.h>

#include "libls/vector.h"

#include "value.h"
#include "op.h"
#include "vm.h"

typedef LS_VECTOR_OF(size_t) FixupList;

struct Parser {
    Lexer *lex;
    bool expr_end;
    LS_VECTOR_OF(Instr) chunk;
    LS_VECTOR_OF(FixupList) fixup_cond;
    LS_VECTOR_OF(FixupList) fixup_cycle;
    LS_VECTOR_OF(size_t) cont_cycle;
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
        .fixup_cond = LS_VECTOR_NEW(),
        .fixup_cycle = LS_VECTOR_NEW(),
        .cont_cycle = LS_VECTOR_NEW(),
    };
    return p;
}

void
parser_reset(Parser *p)
{
    p->expr_end = false;

    LS_VECTOR_CLEAR(p->chunk);

    for (size_t i = 0; i < p->fixup_cond.size; ++i) {
        LS_VECTOR_FREE(p->fixup_cond.data[i]);
    }
    LS_VECTOR_CLEAR(p->fixup_cycle);

    for (size_t i = 0; i < p->fixup_cycle.size; ++i) {
        LS_VECTOR_FREE(p->fixup_cycle.data[i]);
    }
    LS_VECTOR_CLEAR(p->fixup_cycle);

    LS_VECTOR_CLEAR(p->cont_cycle);
}

typedef enum {
    STOP_TOK_OP,
    STOP_TOK_RBRACE,
    STOP_TOK_RBRACKET,
    STOP_TOK_COMMA,
    STOP_TOK_SEMICOLON,
    STOP_TOK_EQ,

    STOP_TOK_IF,
    STOP_TOK_THEN,
    STOP_TOK_ELIF,
    STOP_TOK_ELSE,
    STOP_TOK_WHILE,
    STOP_TOK_DO,
    STOP_TOK_BREAK,
    STOP_TOK_NEXT,
    STOP_TOK_END,

    STOP_TOK_EOF,
} StopTokenKind;

static LS_ATTR_NORETURN
void
throw_at(Parser *p, Lexem pos, const char *msg)
{
    p->err = (ParserError) {.has_pos = true, .pos = pos, .msg = msg};
    longjmp(p->err_handler, 1);
}

#define INSTR(P_, Cmd_, ...) \
    LS_VECTOR_PUSH((P_)->chunk, ((Instr) {.cmd = Cmd_, .args = {__VA_ARGS__}}))

#define INSTR_N(P_, Cmd_) \
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
            lexer_rollback(p->lex);
            throw_at(p, lexer_next(p->lex), "expected either ',' or ';' or ']'");
        }
    }
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
                INSTR(p, CMD_PUSH_SCALAR, .scalar = scalar);
                p->expr_end = true;
            }
            break;

        case LEX_KIND_IDENT:
            {
                THIS_IS_EXPR(p, m);
                INSTR(p, CMD_LOAD, .varname = {.start = m.start, .size = m.size});
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

                if ((int) op->priority < min_priority) {
                    lexer_rollback(p->lex);
                    return STOP_TOK_OP;
                }

                if (op->arity == 1) {
                    if (op->assoc == OP_ASSOC_LEFT) {
                        AFTER_EXPR(p, m);
                        INSTR(p, CMD_OP_UNARY, .unary = op->exec.unary);
                    } else {
                        THIS_IS_EXPR(p, m);
                        StopTokenKind s = expr(p, op->priority);
                        INSTR(p, CMD_OP_UNARY, .unary = op->exec.unary);
                        if (s != STOP_TOK_OP) {
                            return s;
                        }
                    }
                } else {
                    AFTER_EXPR(p, m);
                    p->expr_end = false;
                    StopTokenKind s = expr(p, op->priority + (op->assoc == OP_ASSOC_LEFT));
                    INSTR(p, CMD_OP_BINARY, .binary = op->exec.binary);
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
                            lexer_rollback(p->lex);
                            throw_at(p, lexer_next(p->lex), "expected either ',' or ')'");
                        }
                    }
                }
                INSTR(p, CMD_CALL, .nargs = nargs);
            } else {
                // grouping
                if (expr(p, -1) != STOP_TOK_RBRACE) {
                    lexer_rollback(p->lex);
                    throw_at(p, lexer_next(p->lex), "expected ')'");
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
                        lexer_rollback(p->lex);
                        throw_at(p, lexer_next(p->lex), "expected either ',' or ']'");
                    }
                }
                INSTR(p, CMD_LOAD_AT, .nindices = nindices);
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
                            lexer_rollback(p->lex);
                            throw_at(p, lexer_next(p->lex), "wrong row length");
                        }
                    }
                }
                INSTR(p, CMD_MATRIX, .dims = {.height = height, .width = width});
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

        case LEX_KIND_IF:
            return STOP_TOK_IF;

        case LEX_KIND_ELIF:
            return STOP_TOK_ELIF;

        case LEX_KIND_ELSE:
            return STOP_TOK_ELSE;

        case LEX_KIND_THEN:
            AFTER_EXPR(p, m);
            p->expr_end = false;
            return STOP_TOK_THEN;

        case LEX_KIND_WHILE:
            return STOP_TOK_WHILE;

        case LEX_KIND_DO:
            AFTER_EXPR(p, m);
            p->expr_end = false;
            return STOP_TOK_DO;

        case LEX_KIND_BREAK:
            return STOP_TOK_BREAK;

        case LEX_KIND_NEXT:
            return STOP_TOK_NEXT;

        case LEX_KIND_END:
            AFTER_EXPR(p, m);
            p->expr_end = false;
            return STOP_TOK_END;
        }
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
            if (!p->fixup_cycle.size) {
                throw_at(p, m, "':break' outside of a cycle");
            }
            LS_VECTOR_PUSH(p->fixup_cycle.data[p->fixup_cycle.size - 1], p->chunk.size);
            INSTR_N(p, CMD_JUMP);

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
        break;

    case LEX_KIND_NEXT:
        {
            if (!p->cont_cycle.size) {
                throw_at(p, m, "':next' outside of a cycle");
            }
            INSTR(p, CMD_JUMP, .pos = p->cont_cycle.data[p->cont_cycle.size - 1]);
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
        break;

    case LEX_KIND_IF:
        {
            if (expr(p, -1) != STOP_TOK_THEN) {
                lexer_rollback(p->lex);
                throw_at(p, lexer_next(p->lex), "expected ':then'");
            }

            LS_VECTOR_PUSH(p->fixup_cond, (FixupList) LS_VECTOR_NEW());
#define OUR_FIXUP_LIST p->fixup_cond.data[p->fixup_cond.size - 1]

            size_t prev_jump_unless = p->chunk.size;
            INSTR_N(p, CMD_JUMP_UNLESS);

            bool else_seen = false;
            while (1) {
                StopTokenKind s;
                while ((s = stmt(p)) == STOP_TOK_SEMICOLON) {}
                if (s == STOP_TOK_END) {
                    break;

                } else if (s == STOP_TOK_ELIF) {
                    if (else_seen) {
                        lexer_rollback(p->lex);
                        throw_at(p, lexer_next(p->lex), "':elif' after ':else'");
                    }

                    LS_VECTOR_PUSH(OUR_FIXUP_LIST, p->chunk.size);
                    INSTR_N(p, CMD_JUMP);

                    p->chunk.data[prev_jump_unless].args.pos = p->chunk.size;

                    if (expr(p, -1) != STOP_TOK_THEN) {
                        lexer_rollback(p->lex);
                        throw_at(p, lexer_next(p->lex), "expected ':then'");
                    }
                    prev_jump_unless = p->chunk.size;
                    INSTR_N(p, CMD_JUMP_UNLESS);

                } else if (s == STOP_TOK_ELSE) {
                    if (else_seen) {
                        lexer_rollback(p->lex);
                        throw_at(p, lexer_next(p->lex), "double ':else'");
                    }

                    LS_VECTOR_PUSH(OUR_FIXUP_LIST, p->chunk.size);
                    INSTR_N(p, CMD_JUMP);

                    p->chunk.data[prev_jump_unless].args.pos = p->chunk.size;

                    prev_jump_unless = (size_t) -1;

                    else_seen = true;

                } else {
                    lexer_rollback(p->lex);
                    throw_at(p, lexer_next(p->lex), "expected ':elif:, ':else' or ':end'");
                }
            }

            const size_t end_pos = p->chunk.size;
            if (prev_jump_unless != (size_t) -1) {
                p->chunk.data[prev_jump_unless].args.pos = end_pos;
            }

            FixupList fl = OUR_FIXUP_LIST;
            for (size_t i = 0; i < fl.size; ++i) {
                p->chunk.data[fl.data[i]].args.pos = end_pos;
            }
            LS_VECTOR_FREE(fl);
            --p->fixup_cond.size;
#undef OUR_FIXUP_LIST

            p->expr_end = false;

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
        break;

    case LEX_KIND_WHILE:
        {
            const size_t check_instr = p->chunk.size;

            LS_VECTOR_PUSH(p->cont_cycle, check_instr);

            LS_VECTOR_PUSH(p->fixup_cycle, (FixupList) LS_VECTOR_NEW());

            if (expr(p, -1) != STOP_TOK_DO) {
                lexer_rollback(p->lex);
                throw_at(p, lexer_next(p->lex), "expected ':do'");
            }

            const size_t jump_instr = p->chunk.size;
            INSTR_N(p, CMD_JUMP_UNLESS);

            StopTokenKind s;
            while ((s = stmt(p)) == STOP_TOK_SEMICOLON) {}
            if (s != STOP_TOK_END) {
                lexer_rollback(p->lex);
                throw_at(p, lexer_next(p->lex), "expected ':end'");
            }

            INSTR(p, CMD_JUMP, .pos = check_instr);

            const size_t end_pos = p->chunk.size;
            p->chunk.data[jump_instr].args.pos = end_pos;
            FixupList fl = p->fixup_cycle.data[p->fixup_cycle.size - 1];
            for (size_t i = 0; i < fl.size; ++i) {
                p->chunk.data[fl.data[i]].args.pos = end_pos;
            }
            LS_VECTOR_FREE(fl);
            --p->fixup_cycle.size;
            --p->cont_cycle.size;

            p->expr_end = false;

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
        break;

    default:
        {
            lexer_rollback(p->lex);
            StopTokenKind s = expr(p, -1);
            switch (s) {
            case STOP_TOK_SEMICOLON:
            case STOP_TOK_EOF:
                INSTR_N(p, CMD_PRINT);
                return s;
            case STOP_TOK_EQ:
                {
                    Instr last = p->chunk.data[p->chunk.size - 1];
                    switch (last.cmd) {
                    case CMD_LOAD:
                        last.cmd = CMD_STORE;
                        break;
                    case CMD_LOAD_AT:
                        last.cmd = CMD_STORE_AT;
                        break;
                    default:
                        lexer_rollback(p->lex);
                        throw_at(p, lexer_next(p->lex), "invalid assignment");
                    }
                    --p->chunk.size;
                    StopTokenKind s2 = expr(p, -1);
                    LS_VECTOR_PUSH(p->chunk, last);
                    switch (s2) {
                    case STOP_TOK_SEMICOLON:
                    case STOP_TOK_EOF:
                        return s2;
                    default:
                        lexer_rollback(p->lex);
                        throw_at(p, lexer_next(p->lex), "syntax error");
                    }
                }
                break;
            default:
                lexer_rollback(p->lex);
                throw_at(p, lexer_next(p->lex), "syntax error");
            }
        }
    }
}

bool
parser_parse_expr(Parser *p)
{
    if (setjmp(p->err_handler) != 0) {
        return false;
    }

    StopTokenKind s2;
    while ((s2 = stmt(p)) == STOP_TOK_SEMICOLON) {}
    if (s2 != STOP_TOK_EOF) {
        lexer_rollback(p->lex);
        throw_at(p, lexer_next(p->lex), "syntax error");
    }
    INSTR_N(p, CMD_HALT);
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
    free(p);
}
