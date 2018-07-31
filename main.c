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
Value
X_mat(Env *e, const Value *args, unsigned nargs)
{
    if (nargs != 2) {
        env_throw(e, "'Mat' expects exactly two arguments");
    }
    if (args[0].kind != VAL_KIND_SCALAR || args[1].kind != VAL_KIND_SCALAR) {
        env_throw(e, "both arguments to 'Mat' must be scalars");
    }
    const unsigned height = args[0].as.scalar;
    const unsigned width = args[1].as.scalar;
    if ((height == 0) != (width == 0)) {
        env_throw(e, "invalid matrix dimensions");
    }
    return MAT(matrix_new(height, width));
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
                    printf("\t%.15g", m->elems[elem_idx++]);
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

static
void
destroy_op(void *userdata, LexemKind kind, void *data)
{
    (void) userdata;
    switch (kind) {
    case LEX_KIND_OP:
        free(data);
        break;
    case LEX_KIND_AMBIG_OP:
        {
            AmbigOp *amb = data;
            free(amb->prefix);
            free(amb->infix);
            free(amb);
        }
        break;
    default:
        break;
    }
}

int
main()
{
    Trie *ops = trie_new(128);
#define DUP_OBJ(T_, ...) ls_xmemdup((T_[1]) {__VA_ARGS__}, sizeof(T_))
#define REG_OP(Sym_, ...) \
    trie_insert(ops, Sym_, LEX_KIND_OP, DUP_OBJ(Op, __VA_ARGS__))
#define REG_AMBIG_OP(Sym_, ...) \
    trie_insert(ops, Sym_, LEX_KIND_AMBIG_OP, DUP_OBJ(AmbigOp, __VA_ARGS__))
#define UNARY(Exec_, ...) {.arity = 1, .exec = {.unary = Exec_}, __VA_ARGS__}
#define BINARY(Exec_, ...) {.arity = 2, .exec = {.binary = Exec_}, __VA_ARGS__}

    REG_AMBIG_OP("-", {
        .prefix = DUP_OBJ(Op, UNARY(X_uminus, .assoc = OP_ASSOC_RIGHT, .priority = 100)),
        .infix = DUP_OBJ(Op, BINARY(X_bminus,  .assoc = OP_ASSOC_LEFT, .priority = 1)),
    });
    REG_OP("+", BINARY(X_plus, .assoc = OP_ASSOC_LEFT, .priority = 1));
    REG_OP("*", BINARY(X_mul, .assoc = OP_ASSOC_LEFT, .priority = 2));
    REG_OP("/", BINARY(X_div, .assoc = OP_ASSOC_LEFT, .priority = 2));
    REG_OP("^", BINARY(X_pow, .assoc = OP_ASSOC_RIGHT, .priority = 3));
    REG_OP("!", UNARY(X_fact, .assoc = OP_ASSOC_LEFT, .priority = 4));
    // inv, rank, det, kernel, image, LU, T[ranspose], tr[ace], solve,
    // eigenvalues, eigenvectors, eigenspaces, def (=> -1, -0.5, 0, 0.5, 1),
    // conjT
    // Mat(n, m)

    Lexer *lex = lexer_new(ops);
    Parser *parser = parser_new(lex);
    Ht *ht = ht_new(6);
#define NAME(S_) S_, strlen(S_)
    ht_put(ht, NAME("sin"), CFUNC(X_sin));
    ht_put(ht, NAME("sum"), CFUNC(X_sum));
    ht_put(ht, NAME("Mat"), CFUNC(X_mat));
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
    trie_traverse(ops, destroy_op, NULL);
    trie_destroy(ops);
    lexer_destroy(lex);
    parser_destroy(parser);
    ht_destroy(ht);
    env_destroy(env);
    return EXIT_SUCCESS;
}
