#ifndef value_h_
#define value_h_

#include "common.h"

struct Env;

typedef enum {
    VAL_KIND_SCALAR,
    VAL_KIND_MATRIX,
    VAL_KIND_CFUNC,
} ValueKind;

LS_INHEADER
const char *
value_kindname(ValueKind kind)
{
    switch (kind) {
    case VAL_KIND_SCALAR:
        return "scalar";
    case VAL_KIND_MATRIX:
        return "matrix";
    case VAL_KIND_CFUNC:
        return "function";
    default:
        LS_UNREACHABLE();
    }
}

typedef double Scalar;

typedef struct {
    unsigned nrefs;
} GcObject;

typedef struct {
    GcObject gchdr;
    unsigned height;
    unsigned width;
    Scalar elems[];
} Matrix;

typedef struct Value {
    ValueKind kind;
    union {
        Scalar scalar;
        GcObject *gcobj;
        struct Value (*cfunc)(struct Env *e, const struct Value *args, unsigned nargs);
    } as;
} Value;

LS_INHEADER
void
value_ref(Value v)
{
    switch (v.kind) {
    case VAL_KIND_MATRIX:
        ++v.as.gcobj->nrefs;
        break;
    default:
        break;
    }
}

LS_INHEADER
void
value_unref(Value v)
{
    switch (v.kind) {
    case VAL_KIND_MATRIX:
        if (!--v.as.gcobj->nrefs) {
            free(v.as.gcobj);
        }
        break;
    default:
        break;
    }
}

bool
scalar_parse(const char *buf, size_t nbuf, Scalar *result);

Matrix *
matrix_new(unsigned height, unsigned width);

Matrix *
matrix_construct(struct Env *e, const Value *elems, unsigned height, unsigned width);

Value
matrix_get1(struct Env *e, Matrix *m, Value elem);

Value
matrix_get2(struct Env *e, Matrix *m, Value row, Value col);

void
matrix_set1(struct Env *e, Matrix *m, Value elem, Value v);

void
matrix_set2(struct Env *e, Matrix *m, Value row, Value col, Value v);

#endif
