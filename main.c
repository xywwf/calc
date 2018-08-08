#include "common.h"

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "runtime.h"
#include "op.h"
#include "trie.h"
#include "env.h"
#include "value.h"
#include "matrix.h"
#include "func.h"
#include "str.h"
#include "disasm.h"

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
        return MK_SCL(-a.as.scalar);
    case VAL_KIND_MATRIX:
        {
            Matrix *x = AS_MAT(a);
            Matrix *y = matrix_new(x->height, x->width);
            const size_t n = (size_t) x->height * x->width;
            for (size_t i = 0; i < n; ++i) {
                y->elems[i] = -x->elems[i];
            }
            return MK_MAT(y);
        }
    default:
        env_throw(e, "cannot negate %s value", value_kindname(a.kind));
    }
}

static
Value
X_bminus(Env *e, Value subtrahend, Value minuend)
{
    if (subtrahend.kind == VAL_KIND_MATRIX && minuend.kind == VAL_KIND_MATRIX) {
        Matrix *x = AS_MAT(subtrahend);
        Matrix *y = AS_MAT(minuend);
        if (!eqdim(x, y)) {
            env_throw(e, "matrices unconformable for subtraction");
        }
        Matrix *z = matrix_new(x->height, x->width);
        const size_t n = (size_t) x->height * x->width;
        for (size_t i = 0; i < n; ++i) {
            z->elems[i] = x->elems[i] - y->elems[i];
        }
        return MK_MAT(z);
    } else if (subtrahend.kind == VAL_KIND_SCALAR && minuend.kind == VAL_KIND_SCALAR) {
        return MK_SCL(subtrahend.as.scalar - minuend.as.scalar);
    } else {
        env_throw(e, "cannot subtract %s from %s",
                  value_kindname(subtrahend.kind), value_kindname(minuend.kind));
    }
}

static
Value
X_plus(Env *e, Value a, Value b)
{
    if (a.kind == VAL_KIND_MATRIX && b.kind == VAL_KIND_MATRIX) {
        Matrix *x = AS_MAT(a);
        Matrix *y = AS_MAT(b);
        if (!eqdim(x, y)) {
            env_throw(e, "matrices unconformable for addition");
        }
        Matrix *z = matrix_new(x->height, x->width);
        const size_t n = (size_t) x->height * x->width;
        for (size_t i = 0; i < n; ++i) {
            z->elems[i] = x->elems[i] + y->elems[i];
        }
        return MK_MAT(z);
    } else if (a.kind == VAL_KIND_SCALAR && b.kind == VAL_KIND_SCALAR) {
        return MK_SCL(a.as.scalar + b.as.scalar);
    } else {
        env_throw(e, "cannot add %s to %s", value_kindname(a.kind), value_kindname(b.kind));
    }
}

static inline
Value
sbym(Value s, Value m)
{
    Matrix *x = AS_MAT(m);
    const Scalar a = s.as.scalar;
    Matrix *y = matrix_new(x->height, x->width);
    const size_t n = (size_t) x->height * x->width;
    for (size_t i = 0; i < n; ++i) {
        y->elems[i] = a * x->elems[i];
    }
    return MK_MAT(y);
}

static
Value
X_mul(Env *e, Value a, Value b)
{
    if (a.kind == VAL_KIND_MATRIX && b.kind == VAL_KIND_MATRIX) {
        Matrix *x = AS_MAT(a);
        Matrix *y = AS_MAT(b);
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
        return MK_MAT(z);
    } else if (a.kind == VAL_KIND_SCALAR && b.kind == VAL_KIND_SCALAR) {
        return MK_SCL(a.as.scalar * b.as.scalar);
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
    return MK_SCL(a.as.scalar / b.as.scalar);
}

static
Value
X_mod(Env *e, Value a, Value b)
{
    if (a.kind != VAL_KIND_SCALAR || b.kind != VAL_KIND_SCALAR) {
        env_throw(e, "cannot calculate remainder of %s divided by %s",
                  value_kindname(a.kind), value_kindname(b.kind));
    }
    return MK_SCL(fmod(a.as.scalar, b.as.scalar));
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
        return MK_SCL(a.as.scalar Op_ b.as.scalar); \
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
        return MK_SCL(0);
    }
    switch (a.kind) {
    case VAL_KIND_NIL:
        return MK_SCL(1);
    case VAL_KIND_SCALAR:
        return MK_SCL(a.as.scalar == b.as.scalar);
    case VAL_KIND_MATRIX:
        {
            Matrix *x = AS_MAT(a);
            Matrix *y = AS_MAT(b);
            if (!eqdim(x, y)) {
                return MK_SCL(0);
            }
            const size_t nelems = (size_t) x->height * x->width;
            for (size_t i = 0; i < nelems; ++i) {
                if (x->elems[i] != y->elems[i]) {
                    return MK_SCL(0);
                }
            }
            return MK_SCL(1);
        }
        break;
    case VAL_KIND_CFUNC:
        return MK_SCL(a.as.cfunc == b.as.cfunc);
    case VAL_KIND_FUNC:
        return MK_SCL(a.as.gcobj == b.as.gcobj);
    case VAL_KIND_STR:
        return MK_SCL(str_eq(AS_STR(a), AS_STR(b)));
    }
    LS_UNREACHABLE();
}

static
Value
X_ne(Env *e, Value a, Value b)
{
    (void) e;
    if (a.kind != b.kind) {
        return MK_SCL(1);
    }
    switch (a.kind) {
    case VAL_KIND_NIL:
        return MK_SCL(0);
    case VAL_KIND_SCALAR:
        return MK_SCL(a.as.scalar != b.as.scalar);
    case VAL_KIND_MATRIX:
        {
            Matrix *x = AS_MAT(a);
            Matrix *y = AS_MAT(b);
            if (!eqdim(x, y)) {
                return MK_SCL(1);
            }
            const size_t nelems = (size_t) x->height * x->width;
            for (size_t i = 0; i < nelems; ++i) {
                if (x->elems[i] != y->elems[i]) {
                    return MK_SCL(1);
                }
            }
            return MK_SCL(0);
        }
        break;
    case VAL_KIND_CFUNC:
        return MK_SCL(a.as.cfunc != b.as.cfunc);
    case VAL_KIND_FUNC:
        return MK_SCL(a.as.gcobj != b.as.gcobj);
    case VAL_KIND_STR:
        return MK_SCL(!str_eq(AS_STR(a), AS_STR(b)));
    }
    LS_UNREACHABLE();
}

static
Value
X_not(Env *e, Value a)
{
    (void) e;
    return MK_SCL(!value_is_truthy(a));
}

static
Value
X_and(Env *e, Value a, Value b)
{
    (void) e;
    return MK_SCL(value_is_truthy(a) && value_is_truthy(b));
}

static
Value
X_or(Env *e, Value a, Value b)
{
    (void) e;
    return MK_SCL(value_is_truthy(a) || value_is_truthy(b));
}

static
Value
X_pow(Env *e, Value a, Value b)
{
    if (a.kind != VAL_KIND_SCALAR || b.kind != VAL_KIND_SCALAR) {
        env_throw(e, "cannot raise %s to power of %s",
                  value_kindname(a.kind), value_kindname(b.kind));
    }
    return MK_SCL(pow(a.as.scalar, b.as.scalar));
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
        return MK_SCL(Name_(args[0].as.scalar)); \
    }

DECL1(sin)
DECL1(cos)
DECL1(tan)
DECL1(asin)
DECL1(acos)
DECL1(atan)
DECL1(exp)
DECL1(log)
DECL1(floor)
DECL1(trunc)
DECL1(ceil)
DECL1(round)

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
    return MK_MAT(matrix_new(height, width));
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
    Matrix *m = AS_MAT(args[0]);
    Matrix *d = matrix_new(1, 2);
    d->elems[0] = m->height;
    d->elems[1] = m->width;
    return MK_MAT(d);
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
    Matrix *x = AS_MAT(args[0]);
    const unsigned height = x->height;
    const unsigned width = x->width;
    Matrix *y = matrix_new(width, height);
    for (unsigned i = 0; i < width; ++i) {
        for (unsigned j = 0; j < height; ++j) {
            y->elems[i * height + j] = x->elems[j * width + i];
        }
    }
    return MK_MAT(y);
}

static
Value
X_Rand(Env *e, const Value *args, unsigned nargs)
{
    // Not thread-safe. Nobody cares.
    enum { NBUF = 2048 };
    static unsigned buf[NBUF];
    static unsigned *cur = buf + NBUF;
    static int fd = -1;
    if (fd == -1) {
        fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0) {
            perror("open: /dev/urandom");
            abort();
        }
    }

    (void) e;
    (void) args;
    if (nargs != 0) {
        env_throw(e, "'Rand' takes no arguments");
    }

    if (cur == buf + NBUF) {
        if (read(fd, buf, sizeof(buf)) != sizeof(buf)) {
            env_throw(e, "cannot read from /dev/urandom");
        }
        cur = buf;
    }

    unsigned u = *cur++;
    return MK_SCL(u / (Scalar) UINT32_MAX);
}

static
Value
X_DisAsm(Env *e, const Value *args, unsigned nargs)
{
    if (nargs != 1) {
        env_throw(e, "'DisAsm' expects exactly one argument");
    }
    if (args[0].kind != VAL_KIND_FUNC) {
        env_throw(e, "'DisAsm' can only be applied to a function");
    }
    Func *f = (Func *) args[0].as.gcobj;
    disasm_print(f->chunk, f->nchunk);
    return MK_NIL();
}

static
Value
X_Kind(Env *e, const Value *args, unsigned nargs)
{
    if (nargs != 1) {
        env_throw(e, "'Kind' expects exactly one argument");
    }
    const char *kind = value_kindname(args[0].kind);
    return MK_STR(str_new(kind, strlen(kind)));
}

static
void
append_str_repr(LSString *buf, Value v)
{
    switch (v.kind) {
    case VAL_KIND_NIL:
        ls_string_append_s(buf, "nil");
        break;
    case VAL_KIND_SCALAR:
        ls_string_append_f(buf, "%.15g", v.as.scalar);
        break;
    case VAL_KIND_STR:
        {
            Str *s = (Str *) v.as.gcobj;
            ls_string_append_b(buf, s->data, s->ndata);
        }
        break;
    case VAL_KIND_MATRIX:
        ls_string_append_f(buf, "<matrix %p>", (void *) v.as.gcobj);
        break;
    case VAL_KIND_FUNC:
        ls_string_append_f(buf, "<function %p>", (void *) v.as.gcobj);
        break;
    case VAL_KIND_CFUNC:
        ls_string_append_f(buf, "<build-in function %p>", *(void **) &v.as.gcobj);
        break;
    }
}

static
Value
X_concat(Env *e, Value a, Value b)
{
    (void) e;
    LSString buf = LS_VECTOR_NEW();
    append_str_repr(&buf, a);
    append_str_repr(&buf, b);
    Str *s = str_new(buf.data, buf.size);
    LS_VECTOR_FREE(buf);
    return MK_STR(s);
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
bool
dostring(Runtime rt, const char *name, const char *buf, size_t nbuf)
{
    ExecError err = runtime_exec(rt, buf, nbuf);
    switch (err.kind) {
    case ERR_KIND_OK:
        return true;
    case ERR_KIND_CTIME_HAS_POS:
        {
            const char *pos = err.pos.start;
            size_t line = 1, column = 1;
            for (const char *ptr = buf; ptr != pos; ++ptr) {
                if (*ptr == '\n') {
                    ++line;
                    column = 1;
                } else {
                    ++column;
                }
            }
            fprintf(stderr, "%s:%zu:%zu: %s\n", name, line, column, err.msg);
        }
        return false;
    case ERR_KIND_CTIME_NO_POS:
    case ERR_KIND_RTIME_NO_POS:
        fprintf(stderr, "%s: %s\n", name, err.msg);
        return false;
    }
    LS_UNREACHABLE();
}

static
bool
dofd(Runtime rt, const char *name, int fd)
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
            perror(name);
            return false;
        } else if (r == 0) {
            break;
        }
        size += r;
    }
    const bool r = dostring(rt, name, buf, size);
    free(buf);
    return r;
}

static
bool
dofile(Runtime rt, const char *path)
{
    const int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        return false;
    }
    const bool r = dofd(rt, path, fd);
    close(fd);
    return r;
}

static
void
interactive(Runtime rt)
{
    while (1) {
        char *expr = readline("≈≈> ");
        if (!expr) {
            fputc('\n', stderr);
            return;
        }
        add_history(expr);
        const size_t nexpr = strlen(expr);

        ExecError err = runtime_exec(rt, expr, nexpr);
        switch (err.kind) {
        case ERR_KIND_OK:
            break;
        case ERR_KIND_CTIME_HAS_POS:
            {
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
            }
            break;
        case ERR_KIND_CTIME_NO_POS:
        case ERR_KIND_RTIME_NO_POS:
            fprintf(stderr, "%s\n", err.msg);
        }
        free(expr);
    }
}

static
void
usage(void)
{
    fprintf(stderr, "USAGE: main [-i] [FILE ...]\n"
                    "       main -c CODE\n"
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

    Runtime rt = runtime_new();
    rt.dflag = dflag;

#define UNARY(Exec_, ...) (Op) {.arity = 1, .exec = {.unary = Exec_}, __VA_ARGS__}
#define BINARY(Exec_, ...) (Op) {.arity = 2, .exec = {.binary = Exec_}, __VA_ARGS__}

    runtime_reg_ambig_op(rt, "-",
        UNARY(X_uminus, .assoc = OP_ASSOC_RIGHT, .priority = 100),
        BINARY(X_bminus, .assoc = OP_ASSOC_LEFT, .priority = 1)
    );
    runtime_reg_op(rt, "+", BINARY(X_plus, .assoc = OP_ASSOC_LEFT, .priority = 1));

    runtime_reg_op(rt, "*", BINARY(X_mul, .assoc = OP_ASSOC_LEFT, .priority = 2));
    runtime_reg_op(rt, "/", BINARY(X_div, .assoc = OP_ASSOC_LEFT, .priority = 2));
    runtime_reg_op(rt, "%", BINARY(X_mod, .assoc = OP_ASSOC_LEFT, .priority = 2));
    runtime_reg_op(rt, "^", BINARY(X_pow, .assoc = OP_ASSOC_RIGHT, .priority = 3));

    runtime_reg_op(rt, "~~", BINARY(X_concat, .assoc = OP_ASSOC_LEFT,  .priority = 0));

    runtime_reg_op(rt, "!",  UNARY(X_not,  .assoc = OP_ASSOC_RIGHT, .priority = 0));
    runtime_reg_op(rt, "&&", BINARY(X_and, .assoc = OP_ASSOC_LEFT,  .priority = 0));
    runtime_reg_op(rt, "||", BINARY(X_or,  .assoc = OP_ASSOC_LEFT,  .priority = 0));

    runtime_reg_op(rt, "<",  BINARY(X_lt, .assoc = OP_ASSOC_LEFT, .priority = 0));
    runtime_reg_op(rt, "<=", BINARY(X_le, .assoc = OP_ASSOC_LEFT, .priority = 0));
    runtime_reg_op(rt, "==", BINARY(X_eq, .assoc = OP_ASSOC_LEFT, .priority = 0));
    runtime_reg_op(rt, "!=", BINARY(X_ne, .assoc = OP_ASSOC_LEFT, .priority = 0));
    runtime_reg_op(rt, ">",  BINARY(X_gt, .assoc = OP_ASSOC_LEFT, .priority = 0));
    runtime_reg_op(rt, ">=", BINARY(X_ge, .assoc = OP_ASSOC_LEFT, .priority = 0));

    trie_insert(rt.ops, "=",  LEX_KIND_EQ,       NULL);
    trie_insert(rt.ops, ":=", LEX_KIND_COLON_EQ, NULL);
    trie_insert(rt.ops, "|",  LEX_KIND_BAR,      NULL);

    trie_insert(rt.ops, ":if",     LEX_KIND_IF,     NULL);
    trie_insert(rt.ops, ":then",   LEX_KIND_THEN,   NULL);
    trie_insert(rt.ops, ":elif",   LEX_KIND_ELIF,   NULL);
    trie_insert(rt.ops, ":else",   LEX_KIND_ELSE,   NULL);
    trie_insert(rt.ops, ":while",  LEX_KIND_WHILE,  NULL);
    trie_insert(rt.ops, ":for",    LEX_KIND_FOR,    NULL);
    trie_insert(rt.ops, ":do",     LEX_KIND_DO,     NULL);
    trie_insert(rt.ops, ":break",  LEX_KIND_BREAK,  NULL);
    trie_insert(rt.ops, ":next",   LEX_KIND_NEXT,   NULL);
    trie_insert(rt.ops, ":fu",     LEX_KIND_FU,     NULL);
    trie_insert(rt.ops, ":return", LEX_KIND_RETURN, NULL);
    trie_insert(rt.ops, ":exit",   LEX_KIND_EXIT,   NULL);
    trie_insert(rt.ops, ":end",    LEX_KIND_END,    NULL);

    runtime_put(rt, "sin", MK_CFUNC(X_sin));
    runtime_put(rt, "cos", MK_CFUNC(X_cos));
    runtime_put(rt, "tan", MK_CFUNC(X_tan));

    runtime_put(rt, "asin", MK_CFUNC(X_asin));
    runtime_put(rt, "acos", MK_CFUNC(X_acos));
    runtime_put(rt, "atan", MK_CFUNC(X_atan));

    runtime_put(rt, "ln", MK_CFUNC(X_log));
    runtime_put(rt, "exp", MK_CFUNC(X_exp));

    runtime_put(rt, "trunc", MK_CFUNC(X_trunc));
    runtime_put(rt, "floor", MK_CFUNC(X_floor));
    runtime_put(rt, "ceil", MK_CFUNC(X_ceil));
    runtime_put(rt, "round", MK_CFUNC(X_round));

    runtime_put(rt, "Mat", MK_CFUNC(X_Mat));
    runtime_put(rt, "Dim", MK_CFUNC(X_Dim));
    runtime_put(rt, "Trans", MK_CFUNC(X_Transpose));
    runtime_put(rt, "DisAsm", MK_CFUNC(X_DisAsm));
    runtime_put(rt, "Kind", MK_CFUNC(X_Kind));
    runtime_put(rt, "Rand", MK_CFUNC(X_Rand));

    runtime_put(rt, "Pi", MK_SCL(acos(-1)));
    runtime_put(rt, "E", MK_SCL(exp(1)));

    if (argc == optind) {
        if (codearg) {
            if (iflag) {
                usage();
            }
            if (dostring(rt, "(`-c' argument)", codearg, strlen(codearg))) {
                ret = EXIT_SUCCESS;
            }
        } else {
            if (iflag || detect_tty()) {
                interactive(rt);
            } else {
                if (dofd(rt, "(stdin)", 0)) {
                    ret = EXIT_SUCCESS;
                }
            }
        }
    } else {
        if (codearg) {
            usage();
        }
        for (int i = optind; i < argc; ++i) {
            ret = EXIT_SUCCESS;
            if (!dofile(rt, argv[i])) {
                ret = EXIT_FAILURE;
                break;
            }
        }
        if (iflag) {
            interactive(rt);
        }
    }

    runtime_destroy(rt);
    return ret;
}
