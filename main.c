#include <stdio.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "common.h"
#include "op.h"
#include "trie.h"
#include "lexer.h"
#include "parser.h"
#include "env.h"
#include "disasm.h"

#define ASMAT(V_) ((Matrix *) (V_).as.gcobj)

#define SCALAR(X_) ((Value) {.kind = VAL_KIND_SCALAR, .as = {.scalar = X_}})
#define CFUNC(X_) ((Value) {.kind = VAL_KIND_CFUNC, .as = {.cfunc = X_}})
#define MAT(X_) ((Value) {.kind = VAL_KIND_MATRIX, .as = {.gcobj = (GcObject *) X_}})

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
            Matrix *y = matrix_new(x->height, x->width);
            const size_t n = (size_t) x->height * x->width;
            for (size_t i = 0; i < n; ++i) {
                y->elems[i] = -x->elems[i];
            }
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
        Matrix *z = matrix_new(x->height, x->width);
        const size_t n = (size_t) x->height * x->width;
        for (size_t i = 0; i < n; ++i) {
            z->elems[i] = x->elems[i] - y->elems[i];
        }
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
        Matrix *z = matrix_new(x->height, x->width);
        const size_t n = (size_t) x->height * x->width;
        for (size_t i = 0; i < n; ++i) {
            z->elems[i] = x->elems[i] + y->elems[i];
        }
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
    const Scalar a = s.as.scalar;
    Matrix *y = matrix_new(x->height, x->width);
    const size_t n = (size_t) x->height * x->width;
    for (size_t i = 0; i < n; ++i) {
        y->elems[i] = a * x->elems[i];
    }
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
X_mod(Env *e, Value a, Value b)
{
    if (a.kind != VAL_KIND_SCALAR || b.kind != VAL_KIND_SCALAR) {
        env_throw(e, "cannot calculate remainder of %s divided by %s",
                  value_kindname(a.kind), value_kindname(b.kind));
    }
    return SCALAR(fmod(a.as.scalar, b.as.scalar));
}

#define DECLCOMP(Op_, Name_) \
    static \
    Value \
    X_ ## Name_(Env *e, Value a, Value b) \
    { \
        if (a.kind != VAL_KIND_SCALAR || b.kind != VAL_KIND_SCALAR) { \
            env_throw(e, "cannot compare %s and %s", \
                      value_kindname(a.kind), value_kindname(b.kind)); \
        } \
        return SCALAR(a.as.scalar Op_ b.as.scalar); \
    }
DECLCOMP(<,  lt)
DECLCOMP(<=, le)
DECLCOMP(>,  gt)
DECLCOMP(>=, ge)

static
Value
X_eq(Env *e, Value a, Value b)
{
    (void) e;
    if (a.kind != b.kind) {
        return SCALAR(0);
    }
    switch (a.kind) {
    case VAL_KIND_SCALAR:
        return SCALAR(a.as.scalar == b.as.scalar);
    case VAL_KIND_MATRIX:
        {
            Matrix *x = ASMAT(a);
            Matrix *y = ASMAT(b);
            if (!eqdim(x, y)) {
                return SCALAR(0);
            }
            const size_t nelems = (size_t) x->height * x->width;
            for (size_t i = 0; i < nelems; ++i) {
                if (x->elems[i] != y->elems[i]) {
                    return SCALAR(0);
                }
            }
            return SCALAR(1);
        }
        break;
    case VAL_KIND_CFUNC:
        return SCALAR(a.as.cfunc == b.as.cfunc);
    default:
        LS_UNREACHABLE();
    }
}

static
Value
X_ne(Env *e, Value a, Value b)
{
    (void) e;
    if (a.kind != b.kind) {
        return SCALAR(0);
    }
    switch (a.kind) {
    case VAL_KIND_SCALAR:
        return SCALAR(a.as.scalar != b.as.scalar);
    case VAL_KIND_MATRIX:
        {
            Matrix *x = ASMAT(a);
            Matrix *y = ASMAT(b);
            if (!eqdim(x, y)) {
                return SCALAR(1);
            }
            const size_t nelems = (size_t) x->height * x->width;
            for (size_t i = 0; i < nelems; ++i) {
                if (x->elems[i] != y->elems[i]) {
                    return SCALAR(1);
                }
            }
            return SCALAR(0);
        }
        break;
    case VAL_KIND_CFUNC:
        return SCALAR(a.as.cfunc != b.as.cfunc);
    default:
        LS_UNREACHABLE();
    }
}

static
Value
X_not(Env *e, Value a)
{
    if (a.kind != VAL_KIND_SCALAR) {
        env_throw(e, "cannot calculate boolean negation of a %s value", value_kindname(a.kind));
    }
    return SCALAR(!a.as.scalar);
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

#define DECL1(Name_) \
    static \
    Value \
    X_ ## Name_(Env *e, const Value *args, unsigned nargs) \
    { \
        if (nargs != 1) { \
            env_throw(e, "'%s' expects exactly one argument", #Name_); \
        } \
        if (args[0].kind != VAL_KIND_SCALAR) { \
            env_throw(e, "'%s' can only be applied to a scalar", #Name_); \
        } \
        return SCALAR(Name_(args[0].as.scalar)); \
    }

DECL1(sin)
DECL1(cos)
DECL1(atan)
DECL1(exp)
DECL1(log)
DECL1(floor)
DECL1(ceil)

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
X_Mat(Env *e, const Value *args, unsigned nargs)
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
Value
X_Dim(Env *e, const Value *args, unsigned nargs)
{
    if (nargs != 1) {
        env_throw(e, "'Dim' expects exactly one argument");
    }
    if (args[0].kind != VAL_KIND_MATRIX) {
        env_throw(e, "'Dim' can only be applied to a matrix");
    }
    Matrix *m = ASMAT(args[0]);
    Matrix *d = matrix_new(1, 2);
    d->elems[0] = m->height;
    d->elems[1] = m->width;
    return MAT(d);
}

static
Value
X_Transpose(Env *e, const Value *args, unsigned nargs)
{
    if (nargs != 1) {
        env_throw(e, "'Trans' expects exactly one argument");
    }
    if (args[0].kind != VAL_KIND_MATRIX) {
        env_throw(e, "'Trans' can only be applied to a matrix");
    }
    Matrix *x = ASMAT(args[0]);
    const unsigned height = x->height;
    const unsigned width = x->width;
    Matrix *y = matrix_new(width, height);
    for (unsigned i = 0; i < width; ++i) {
        for (unsigned j = 0; j < height; ++j) {
            y->elems[i * height + j] = x->elems[j * width + i];
        }
    }
    return MAT(y);
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

static
bool
detect_tty(void)
{
    if (!isatty(0)) {
        return false;
    }
    const char *term = getenv("TERM");
    if (!term || strcmp(term, "") == 0 || strcmp(term, "dumb") == 0) {
        return false;
    }
    return true;
}

static
char *
full_read(int fd, size_t *nbuf)
{
    char *buf = NULL;
    size_t size = 0;
    size_t capacity = 0;
    while (1) {
        if (size == capacity) {
            buf = ls_x2realloc(buf, &capacity, 1);
        }
        const ssize_t r = read(fd, buf + size, capacity - size);
        if (r < 0) {
            return NULL;
        } else if (r == 0) {
            break;
        }
        size += r;
    }
    *nbuf = size;
    return buf;
}

static
void
usage(void)
{
    fprintf(stderr, "USAGE: main [-i]\n"
                    "       main -c CODE\n"
                    "       main FILE\n"
                    );
    exit(2);
}

int
main(int argc, char **argv)
{
    int ret = EXIT_FAILURE;
    char *codearg = NULL;
    bool iflag = false;
    bool dflag = false;
    for (int c; (c = getopt(argc, argv, "c:id")) != -1;) {
        switch (c) {
        case 'c':
            codearg = optarg;
            break;
        case 'i':
            iflag = true;
            break;
        case 'd':
            dflag = true;
            break;
        case '?':
            usage();
            break;
        default:
            LS_UNREACHABLE();
        }
    }

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
        .infix = DUP_OBJ(Op, BINARY(X_bminus, .assoc = OP_ASSOC_LEFT, .priority = 1)),
    });
    REG_OP("+", BINARY(X_plus, .assoc = OP_ASSOC_LEFT, .priority = 1));
    REG_OP("*", BINARY(X_mul, .assoc = OP_ASSOC_LEFT, .priority = 2));
    REG_OP("/", BINARY(X_div, .assoc = OP_ASSOC_LEFT, .priority = 2));
    REG_OP("%", BINARY(X_mod, .assoc = OP_ASSOC_LEFT, .priority = 2));
    REG_OP("^", BINARY(X_pow, .assoc = OP_ASSOC_RIGHT, .priority = 3));

    REG_OP("!", UNARY(X_not, .assoc = OP_ASSOC_RIGHT, .priority = 0));

    REG_OP("<",  BINARY(X_lt, .assoc = OP_ASSOC_LEFT, .priority = 0));
    REG_OP("<=", BINARY(X_le, .assoc = OP_ASSOC_LEFT, .priority = 0));
    REG_OP("==", BINARY(X_eq, .assoc = OP_ASSOC_LEFT, .priority = 0));
    REG_OP("!=", BINARY(X_ne, .assoc = OP_ASSOC_LEFT, .priority = 0));
    REG_OP(">",  BINARY(X_gt, .assoc = OP_ASSOC_LEFT, .priority = 0));
    REG_OP(">=", BINARY(X_ge, .assoc = OP_ASSOC_LEFT, .priority = 0));

    trie_insert(ops, "=", LEX_KIND_EQ, NULL);

    trie_insert(ops, ":if",     LEX_KIND_IF,     NULL);
    trie_insert(ops, ":then",   LEX_KIND_THEN,   NULL);
    trie_insert(ops, ":elif",   LEX_KIND_ELIF,   NULL);
    trie_insert(ops, ":else",   LEX_KIND_ELSE,   NULL);
    trie_insert(ops, ":while",  LEX_KIND_WHILE,  NULL);
    trie_insert(ops, ":do",     LEX_KIND_DO,     NULL);
    trie_insert(ops, ":break",  LEX_KIND_BREAK,  NULL);
    trie_insert(ops, ":next",   LEX_KIND_NEXT,   NULL);
    trie_insert(ops, ":end",    LEX_KIND_END,    NULL);

    // inv, rank, det, kernel, image, LU, tr[ace], solve
    // eigenvalues, eigenvectors, eigenspaces, def (=> -1, -0.5, 0, 0.5, 1), conjT

    Lexer *lex = lexer_new(ops);
    Parser *parser = parser_new(lex);
    Ht *ht = ht_new(6);
#define NAME(S_) S_, strlen(S_)
    ht_put(ht, NAME("sin"),   CFUNC(X_sin));
    ht_put(ht, NAME("cos"),   CFUNC(X_cos));
    ht_put(ht, NAME("atan"),  CFUNC(X_atan));
    ht_put(ht, NAME("ln"),    CFUNC(X_log));
    ht_put(ht, NAME("exp"),   CFUNC(X_exp));
    ht_put(ht, NAME("floor"), CFUNC(X_floor));
    ht_put(ht, NAME("ceil"),  CFUNC(X_ceil));

    ht_put(ht, NAME("sum"),   CFUNC(X_sum));
    ht_put(ht, NAME("Mat"),   CFUNC(X_Mat));
    ht_put(ht, NAME("Dim"),   CFUNC(X_Dim));
    ht_put(ht, NAME("Trans"), CFUNC(X_Transpose));

    ht_put(ht, NAME("pi"), SCALAR(acos(-1)));

    Env *env = env_new(ht);

    bool interactive = false;
    char *code;
    size_t ncode;
    switch (argc - optind) {
    case 0:
        if (codearg) {
            code = ls_xstrdup(codearg);
            ncode = strlen(code);
        } else {
            if (iflag || detect_tty()) {
                interactive = true;
            } else {
                if (!(code = full_read(0, &ncode))) {
                    perror("<stdin>");
                    goto cleanup;
                }
            }
        }
        break;
    case 1:
        {
            if (codearg || iflag) {
                usage();
            }
            const int fd = open(argv[optind], O_RDONLY);
            if (fd < 0) {
                perror(argv[optind]);
                goto cleanup;
            }
            if (!(code = full_read(fd, &ncode))) {
                perror(argv[optind]);
                goto cleanup;
            }
            close(fd);
        }
        break;
    default:
        usage();
    }

    if (interactive) {
        while (1) {
            char *expr = readline("≈≈> ");
            if (!expr) {
                fputc('\n', stderr);
                ret = EXIT_SUCCESS;
                goto cleanup;
            }
            add_history(expr);
            const size_t nexpr = strlen(expr);

            lexer_reset(lex, expr, nexpr);
            parser_reset(parser);
            if (parser_parse_expr(parser)) {
                size_t nchunk;
                const Instr *chunk = parser_last_chunk(parser, &nchunk);

                if (dflag) {
                    disasm_print(chunk, nchunk);
                } else {
                    if (!env_eval(env, chunk, nchunk)) {
                        fprintf(stderr, "%s\n", env_last_error(env));
                        env_free_last_error(env);
                    }
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

            free(expr);
        }
    } else {
        lexer_reset(lex, code, ncode);
        parser_reset(parser);
        if (parser_parse_expr(parser)) {
            size_t nchunk;
            const Instr *chunk = parser_last_chunk(parser, &nchunk);
            if (dflag) {
                disasm_print(chunk, nchunk);
                ret = EXIT_SUCCESS;
            } else {
                if (env_eval(env, chunk, nchunk)) {
                    ret = EXIT_SUCCESS;
                } else {
                    // TODO output line?
                    fprintf(stderr, "%s\n", env_last_error(env));
                    env_free_last_error(env);
                }
            }
        } else {
            ParserError err = parser_last_error(parser);
            // TODO output line.
            fprintf(stderr, "%s\n", err.msg);
        }
        free(code);
    }

cleanup:
    trie_traverse(ops, destroy_op, NULL);
    trie_destroy(ops);
    lexer_destroy(lex);
    parser_destroy(parser);
    ht_destroy(ht);
    env_destroy(env);
    return ret;
}
