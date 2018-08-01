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

static
void
throw_at(Parser *p, Lexem pos, const char *msg)
{
    p->err = (ParserError) {.has_pos = true, .pos = pos, .msg = msg};
    longjmp(p->err_handler, 1);
}

#define INSTR(P_, Cmd_, ...) \
    LS_VECTOR_PUSH((P_)->chunk, ((Instr) {.cmd = Cmd_, .args = {__VA_ARGS__}}))

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
void
on_ident(Parser *p, Lexem m)
{
    THIS_IS_EXPR(p, m);
    INSTR(p, CMD_PUSH_VAR, .varname = {.start = m.start, .size = m.size});
    p->expr_end = true;
}

static inline
unsigned
on_indexing(Parser *p)
{
    p->expr_end = false;
    for (unsigned nindices = 1; ; ++nindices) {
        switch (expr(p, -1)) {
        case STOP_TOK_COMMA:
            break;
        case STOP_TOK_RBRACKET:
            return nindices;
        default:
            lexer_rollback(p->lex);
            throw_at(p, lexer_next(p->lex), "expected either ',' or ']'");
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
            on_ident(p, m);
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
                unsigned nindices = on_indexing(p);
                INSTR(p, CMD_GET_AT, .nindices = nindices);
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
            throw_at(p, m, "invalid assignment");
            break;
        }
    }
}

static
bool
detect_assignment(Parser *p, Instr *assign)
{
    lexer_mark(p->lex);
    Lexem m1 = lexer_next(p->lex);
    if (m1.kind != LEX_KIND_IDENT) {
        lexer_rollback(p->lex);
        return false;
    }

    lexer_mark(p->lex);
    Lexem m2 = lexer_next(p->lex);
    if (m2.kind == LEX_KIND_EQ) {
        *assign = (Instr) {
            .cmd = CMD_ASSIGN,
            .args = {.varname = {.start = m1.start, .size = m1.size}},
        };
        return true;
    } else if (m2.kind != LEX_KIND_LBRACKET) {
        on_ident(p, m1);
        lexer_rollback(p->lex);
        return false;
    }

    on_ident(p, m1);
    const unsigned nindices = on_indexing(p);
    lexer_mark(p->lex);
    Lexem m3 = lexer_next(p->lex);
    if (m3.kind == LEX_KIND_EQ) {
        p->expr_end = false;
        *assign = (Instr) {
            .cmd = CMD_SET_AT,
            .args = {.nindices = nindices},
        };
        return true;
    }

    INSTR(p, CMD_GET_AT, .nindices = nindices);
    lexer_rollback(p->lex);
    return false;
}

bool
parser_parse_expr(Parser *p)
{
    if (setjmp(p->err_handler) != 0) {
        return false;
    }

    Instr assign;
    bool has_assign = detect_assignment(p, &assign);

    if (expr(p, -1) != STOP_TOK_EOF) {
        lexer_rollback(p->lex);
        p->err = (ParserError) {
            .has_pos = true,
            .pos = lexer_next(p->lex),
            .msg = "syntax error",
        };
        return false;
    }

    if (has_assign) {
        LS_VECTOR_PUSH(p->chunk, assign);
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
