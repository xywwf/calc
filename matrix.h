#ifndef matrix_h_
#define matrix_h_

#include "common.h"
#include "value.h"

#include <stdint.h>
#include <limits.h>

LS_INHEADER
size_t
xmul_mat_dims(unsigned height, unsigned width)
{
    const uint_least64_t res = (uint_least64_t) height * width;
    if (res > UINT_MAX) {
        LS_PANIC("matrix is too large (would run out of memory)");
    }
    return res;
}

struct Env;

typedef struct {
    GcObject gchdr;
    unsigned height;
    unsigned width;
    Scalar elems[];
} Matrix;

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
