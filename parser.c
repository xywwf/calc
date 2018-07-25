#include "parser.h"
#include <setjmp.h>

#include "libls/vector.h"

#include "value.h"
#include "op.h"
#include "vm.h"

struct Parser {
    Lexer *lex;
    bool expr_end;
    LS_VECTOR_OF(Instr) chunk;
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
    };
    return p;
}

void
parser_reset(Parser *p)
{
    p->expr_end = false;
    LS_VECTOR_CLEAR(p->chunk);
}

typedef enum {
    STOP_TOK_OP,
    STOP_TOK_RBRACE,
    STOP_TOK_RBRACKET,
    STOP_TOK_COMMA,
    STOP_TOK_SEMICOLON,
    STOP_TOK_EOF,
} StopTokenKind;

#define INSTR(P_, Cmd_, ...) \
    LS_VECTOR_PUSH((P_)->chunk, ((Instr) {.cmd = Cmd_, .args = {__VA_ARGS__}}))

static
void
throw_at(Parser *p, Lexem pos, const char *msg)
{
    p->err = (ParserError) {.has_pos = true, .pos = pos, .msg = msg};
    longjmp(p->err_handler, 1);
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
            lexer_rollback(p->lex);
            throw_at(p, lexer_next(p->lex), "expected either ',' or ';' or ']'");
        }
    }
}

static inline
StopTokenKind
expr(Parser *p, int min_priority)
{
#define AFTER_EXPR() \
    do { \
        if (!p->expr_end) { \
            throw_at(p, m, "expected expression"); \
        } \
    } while (0)
#define THIS_IS_EXPR() \
    do { \
        if (p->expr_end) { \
            throw_at(p, m, "expected operator or end of expression"); \
        } \
    } while (0)

    while (1) {
        lexer_mark(p->lex);
        Lexem m = lexer_next(p->lex);
        switch (m.kind) {

        case LEX_KIND_NUM:
            {
                THIS_IS_EXPR();
                Scalar scalar;
                if (!scalar_parse(m.start, m.size, &scalar)) {
                    throw_at(p, m, "invalid number");
                }
                INSTR(p, CMD_PUSH_SCALAR, .scalar = scalar);
                p->expr_end = true;
            }
            break;

        case LEX_KIND_IDENT:
            THIS_IS_EXPR();
            INSTR(p, CMD_PUSH_VAR, .varname = {.start = m.start, .size = m.size});
            p->expr_end = true;
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
                        AFTER_EXPR();
                        INSTR(p, CMD_OP_UNARY, .unary = op->exec.unary);
                    } else {
                        THIS_IS_EXPR();
                        StopTokenKind s = expr(p, op->priority);
                        INSTR(p, CMD_OP_UNARY, .unary = op->exec.unary);
                        if (s != STOP_TOK_OP) {
                            return s;
                        }
                    }
                } else {
                    AFTER_EXPR();
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
            {
                THIS_IS_EXPR();
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
            AFTER_EXPR();
            return STOP_TOK_EOF;

        case LEX_KIND_RBRACE:
            AFTER_EXPR();
            return STOP_TOK_RBRACE;

        case LEX_KIND_RBRACKET:
            AFTER_EXPR();
            return STOP_TOK_RBRACKET;

        case LEX_KIND_COMMA:
            AFTER_EXPR();
            p->expr_end = false;
            return STOP_TOK_COMMA;

        case LEX_KIND_SEMICOLON:
            p->expr_end = false;
            return STOP_TOK_SEMICOLON;

        case LEX_KIND_ERROR:
            throw_at(p, m, m.data);
            break;

        case LEX_KIND_EQ:
            throw_at(p, m, "invalid assignment");
            break;
        }
    }
#undef AFTER_EXPR
#undef THIS_IS_EXPR
}

bool
parser_parse_expr(Parser *p)
{
    if (setjmp(p->err_handler) != 0) {
        return false;
    }

    bool assign = false;

    lexer_mark(p->lex);
    Lexem m = lexer_next(p->lex);
    if (m.kind == LEX_KIND_IDENT) {
        assign = lexer_next(p->lex).kind == LEX_KIND_EQ;
    }
    if (!assign) {
        lexer_rollback(p->lex);
    }

    if (expr(p, -1) != STOP_TOK_EOF) {
        lexer_rollback(p->lex);
        p->err = (ParserError) {
            .has_pos = true,
            .pos = lexer_next(p->lex),
            .msg = "syntax error",
        };
        return false;
    }

    if (assign) {
        INSTR(p, CMD_ASSIGN, .varname = {.start = m.start, .size = m.size});
    }
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
