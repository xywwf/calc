#ifndef value_h_
#define value_h_

#include "common.h"

#define MK_NIL() ((Value) {.kind = VAL_KIND_NIL})
#define MK_SCL(X_) ((Value) {.kind = VAL_KIND_SCALAR, .as = {.scalar = X_}})
#define MK_MAT(X_) ((Value) {.kind = VAL_KIND_MATRIX, .as = {.gcobj = (GcObject *) X_}})
#define MK_CFUNC(X_) ((Value) {.kind = VAL_KIND_CFUNC, .as = {.cfunc = X_}})
#define MK_FUNC(X_) ((Value) {.kind = VAL_KIND_FUNC, .as = {.gcobj = (GcObject *) X_}})
#define MK_STR(X_) ((Value) {.kind = VAL_KIND_STR, .as = {.gcobj = (GcObject *) X_}})

#define AS_SCL(X_) (X_).as.scalar
#define AS_MAT(X_) ((Matrix *) (X_).as.gcobj)
#define AS_CFUNC(X_) (X_).as.cfunc
#define AS_FUNC(X_) ((Func *) (X_).as.gcobj)
#define AS_STR(X_) ((Str *) (X_).as.gcobj)

struct Env;

typedef enum {
    VAL_KIND_NIL,
    VAL_KIND_SCALAR,
    VAL_KIND_MATRIX,
    VAL_KIND_CFUNC,
    VAL_KIND_FUNC,
    VAL_KIND_STR,
} ValueKind;

LS_INHEADER
const char *
value_kindname(ValueKind kind)
{
    switch (kind) {
    case VAL_KIND_NIL:
        return "nil";
    case VAL_KIND_SCALAR:
        return "scalar";
    case VAL_KIND_MATRIX:
        return "matrix";
    case VAL_KIND_CFUNC:
        return "built-in function";
    case VAL_KIND_FUNC:
        return "function";
    case VAL_KIND_STR:
        return "string";
    }
    LS_UNREACHABLE();
}

typedef double Scalar;

typedef struct {
    unsigned nrefs;
} GcObject;

typedef struct Value {
    ValueKind kind;
    union {
        Scalar scalar;
        GcObject *gcobj;
        struct Value (*cfunc)(struct Env *e, const struct Value *args, unsigned nargs);
    } as;
} Value;

void
gcobject_destroy(Value v);

LS_INHEADER
void
value_ref(Value v)
{
    switch (v.kind) {
    case VAL_KIND_MATRIX:
    case VAL_KIND_FUNC:
    case VAL_KIND_STR:
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
    case VAL_KIND_FUNC:
    case VAL_KIND_STR:
        if (!--v.as.gcobj->nrefs) {
            gcobject_destroy(v);
        }
        break;
    default:
        break;
    }
}

void
value_print(Value v);

bool
value_is_truthy(Value v);

bool
scalar_parse(const char *buf, size_t nbuf, Scalar *result);

#endif
