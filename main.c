#include <stdio.h>
#include <math.h>
#include <string.h>

#include "libls/vector.h"

#include "common.h"
#include "op.h"
#include "trie.h"
#include "lexer.h"
#include "parser.h"
#include "env.h"

#define ASMAT(V_) ((Matrix *) (V_).as.gcobj)

#define SCALAR(X_) ((Value) {.kind = VAL_KIND_SCALAR, .as = {.scalar = X_}})
#define CFUNC(X_) ((Value) {.kind = VAL_KIND_CFUNC, .as = {.cfunc = X_}})
#define MAT(X_) ((Value) {.kind = VAL_KIND_MATRIX, .as = {.gcobj = (GcObject *) X_}})

#define M1(MVar_, DimRef_, IVar_, ...) \
    Matrix *MVar_ = matrix_new((DimRef_)->height, (DimRef_)->width); \
    do { \
        const size_t n__ = MVar_->height * MVar_->width; \
        for (size_t IVar_ = 0; IVar_ < n__; ++IVar_) { \
            MVar_->elems[IVar_] = (__VA_ARGS__); \
        } \
    } while (0)

static inline
bool
eqdim(Matrix *x, Matrix *y)
{
    return x->height == y->height && x->width == y->width;
}

static
Value
X_uminus(Env *e, Value a)
{
    switch (a.kind) {
    case VAL_KIND_SCALAR:
        return SCALAR(-a.as.scalar);
    case VAL_KIND_MATRIX:
        {
            Matrix *x = ASMAT(a);
            M1(y, x, i, -x->elems[i]);
            return MAT(y);
        }
    default:
        env_throw(e, "cannot negate %s value", value_kindname(a.kind));
    }
}

static
Value
X_bminus(Env *e, Value a, Value b)
{
    if (a.kind == VAL_KIND_MATRIX && b.kind == VAL_KIND_MATRIX) {
        Matrix *x = ASMAT(a);
        Matrix *y = ASMAT(b);
        if (!eqdim(x, y)) {
            env_throw(e, "matrices unconformable for subtraction");
        }
        M1(z, x, i, x->elems[i] - y->elems[i]);
        return MAT(z);
    } else if (a.kind == VAL_KIND_SCALAR && b.kind == VAL_KIND_SCALAR) {
        return SCALAR(a.as.scalar - b.as.scalar);
    } else {
        env_throw(e, "cannot subtract %s from %s", value_kindname(b.kind), value_kindname(a.kind));
    }
}

static
Value
X_plus(Env *e, Value a, Value b)
{
    if (a.kind == VAL_KIND_MATRIX && b.kind == VAL_KIND_MATRIX) {
        Matrix *x = ASMAT(a);
        Matrix *y = ASMAT(b);
        if (!eqdim(x, y)) {
            env_throw(e, "matrices unconformable for addition");
        }
        M1(z, x, i, x->elems[i] + y->elems[i]);
        return MAT(z);
    } else if (a.kind == VAL_KIND_SCALAR && b.kind == VAL_KIND_SCALAR) {
        return SCALAR(a.as.scalar + b.as.scalar);
    } else {
        env_throw(e, "cannot add %s to %s", value_kindname(a.kind), value_kindname(b.kind));
    }
}

static inline
Value
sbym(Value s, Value m)
{
    Matrix *x = ASMAT(m);
    Scalar a = s.as.scalar;
    M1(y, x, i, a * x->elems[i]);
    return MAT(y);
}

static
Value
X_mul(Env *e, Value a, Value b)
{
    if (a.kind == VAL_KIND_MATRIX && b.kind == VAL_KIND_MATRIX) {
        Matrix *x = ASMAT(a);
        Matrix *y = ASMAT(b);
        if (x->width != y->height) {
            env_throw(e, "matrices unconformable for multiplication");
        }
        const unsigned m = x->height;
        const unsigned n = x->width;
        const unsigned p = y->width;
        Matrix *z = matrix_new(m, p);
        for (unsigned i = 0; i < m; ++i) {
            for (unsigned j = 0; j < p; ++j) {
                Scalar elem = 0;
                for (unsigned k = 0; k < n; ++k) {
                    elem += x->elems[i * n + k] * y->elems[k * p + j];
                }
                z->elems[i * p + j] = elem;
            }
        }
        return MAT(z);
    } else if (a.kind == VAL_KIND_SCALAR && b.kind == VAL_KIND_SCALAR) {
        return SCALAR(a.as.scalar * b.as.scalar);
    } else if (a.kind == VAL_KIND_SCALAR && b.kind == VAL_KIND_MATRIX) {
        return sbym(a, b);
    } else if (a.kind == VAL_KIND_MATRIX && b.kind == VAL_KIND_SCALAR) {
        return sbym(b, a);
    } else {
        env_throw(e, "cannot multiply %s by %s", value_kindname(a.kind), value_kindname(b.kind));
    }
}

static
Value
X_div(Env *e, Value a, Value b)
{
    if (a.kind != VAL_KIND_SCALAR || b.kind != VAL_KIND_SCALAR) {
        env_throw(e, "cannot divide %s by %s", value_kindname(a.kind), value_kindname(b.kind));
    }
    return SCALAR(a.as.scalar / b.as.scalar);
}

static
Value
X_pow(Env *e, Value a, Value b)
{
    if (a.kind != VAL_KIND_SCALAR || b.kind != VAL_KIND_SCALAR) {
        env_throw(e, "cannot raise %s to power of %s",
                  value_kindname(a.kind), value_kindname(b.kind));
    }
    return SCALAR(pow(a.as.scalar, b.as.scalar));
}

static
Value
X_fact(Env *e, Value a)
{
    if (a.kind != VAL_KIND_SCALAR) {
        env_throw(e, "cannot calculate factorial of %s", value_kindname(a.kind));
    }
    const Scalar x = a.as.scalar;
    if (x > 1000000) {
        return SCALAR(1. / 0.);
    }
    Scalar r = 1;
    for (Scalar i = 2; i <= x; ++i) {
        r *= i;
    }
    return SCALAR(r);
}

static
Value
X_sin(Env *e, const Value *args, unsigned nargs)
{
    if (nargs != 1) {
        env_throw(e, "wrong number of args to 'sin'");
    }
    if (args[0].kind != VAL_KIND_SCALAR) {
        env_throw(e, "'sin' can only be applied to a scalar");
    }
    return SCALAR(sin(args[0].as.scalar));
}

static
Value
X_sum(Env *e, const Value *args, unsigned nargs)
{
    Scalar r = 0;
    for (unsigned i = 0; i < nargs; ++i) {
        if (args[i].kind != VAL_KIND_SCALAR) {
            env_throw(e, "'sum' can only be applied to scalars");
        }
        r += args[i].as.scalar;
    }
    return SCALAR(r);
}

static
void
print_value(Value v)
{
    switch (v.kind) {
    case VAL_KIND_SCALAR:
        printf(" %.15g\n", v.as.scalar);
        break;
    case VAL_KIND_MATRIX:
        {
            Matrix *m = ASMAT(v);
            size_t elem_idx = 0;
            puts(" [");
            for (unsigned i = 0; i < m->height; ++i) {
                for (unsigned j = 0; j < m->width; ++j) {
                    printf(" \t%.15g", m->elems[elem_idx++]);
                }
                puts("");
            }
            puts(" ]");
        }
        break;
    case VAL_KIND_CFUNC:
        printf(" <built-in function>\n");
        break;
    }
}

typedef struct {
    Trie *trie;
    LS_VECTOR_OF(Op) ops;
    LS_VECTOR_OF(AmbigOp) ambops;
} OpStore;

static inline
Op *
add_op(OpStore *s, Op op)
{
    LS_VECTOR_PUSH(s->ops, op);
    return &s->ops.data[s->ops.size - 1];
}

static inline
void
reg_op(OpStore *s, const char *sym, Op op)
{
    trie_insert(s->trie, sym, LEX_KIND_OP, add_op(s, op));
}

static inline
void
reg_ambig_op(OpStore *s, const char *sym, AmbigOp ambop)
{
    LS_VECTOR_PUSH(s->ambops, ambop);
    trie_insert(s->trie, sym, LEX_KIND_AMBIG_OP, &s->ambops.data[s->ambops.size - 1]);
}

int
main()
{
    OpStore ops = {
        .trie = trie_new(128),
        .ops = LS_VECTOR_NEW(),
        .ambops = LS_VECTOR_NEW(),
    };
#define UNARY(Exec_, ...) (Op) {.arity = 1, .exec = {.unary = Exec_}, __VA_ARGS__}
#define BINARY(Exec_, ...) (Op) {.arity = 2, .exec = {.binary = Exec_}, __VA_ARGS__}
#define NAME(S_) S_, strlen(S_)
    reg_ambig_op(&ops, "-", (AmbigOp) {
        .prefix = add_op(&ops, UNARY(X_uminus, .assoc = OP_ASSOC_RIGHT, .priority = 100)),
        .infix = add_op(&ops, BINARY(X_bminus, .assoc = OP_ASSOC_LEFT, .priority = 1)),
    });
    reg_op(&ops, "+", BINARY(X_plus, .assoc = OP_ASSOC_LEFT, .priority = 1));
    reg_op(&ops, "*", BINARY(X_mul, .assoc = OP_ASSOC_LEFT, .priority = 2));
    reg_op(&ops, "/", BINARY(X_div, .assoc = OP_ASSOC_LEFT, .priority = 2));
    reg_op(&ops, "^", BINARY(X_pow, .assoc = OP_ASSOC_RIGHT, .priority = 3));
    reg_op(&ops, "!", UNARY(X_fact, .assoc = OP_ASSOC_LEFT, .priority = 4));

    Lexer *lex = lexer_new(ops.trie);
    Parser *parser = parser_new(lex);
    Ht *ht = ht_new(6);
    ht_put(ht, NAME("sin"), CFUNC(X_sin));
    ht_put(ht, NAME("sum"), CFUNC(X_sum));
    ht_put(ht, NAME("pi"), SCALAR(acos(-1)));
    Env *env = env_new(ht);

    char *expr = NULL;
    size_t expr_alloc = 0;
    while (1) {
        ssize_t nexpr = getline(&expr, &expr_alloc, stdin);
        if (nexpr < 0) {
            if (feof(stdin)) {
                goto cleanup;
            } else {
                perror("getline");
                return EXIT_FAILURE;
            }
        }

        if (nexpr && expr[nexpr - 1] == '\n') {
            --nexpr;
        }
        lexer_reset(lex, expr, nexpr);
        parser_reset(parser);
        if (parser_parse_expr(parser)) {
            size_t nchunk;
            const Instr *chunk = parser_last_chunk(parser, &nchunk);
            Value result;
            switch (env_eval(env, chunk, nchunk, &result)) {
            case -1:
                fprintf(stderr, "%s\n", env_last_error(env));
                env_free_last_error(env);
                break;
            case 0:
                fprintf(stderr, "ok :)\n");
                break;
            case 1:
                print_value(result);
                value_unref(result);
                break;
            }
        } else {
            ParserError err = parser_last_error(parser);
            if (err.has_pos) {
                fprintf(stderr, "> %.*s\n", (int) nexpr, expr);
                size_t start_pos = err.pos.start - expr;
                size_t end_pos = start_pos + err.pos.size;
                if (end_pos == start_pos) {
                    ++end_pos;
                }
                for (size_t i = 0; i < start_pos; ++i) {
                    expr[i] = ' ';
                }
                expr[start_pos] = '^';
                for (size_t i = start_pos + 1; i < end_pos; ++i) {
                    expr[i] = '~';
                }
                fprintf(stderr, "  %.*s %s\n", (int) end_pos, expr, err.msg);
            } else {
                fprintf(stderr, "%s\n", err.msg);
            }
        }
    }

cleanup:
    free(expr);
    trie_destroy(ops.trie);
    LS_VECTOR_FREE(ops.ops);
    LS_VECTOR_FREE(ops.ambops);
    lexer_destroy(lex);
    parser_destroy(parser);
    ht_destroy(ht);
    env_destroy(env);
    return EXIT_SUCCESS;
}
