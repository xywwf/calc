#ifndef matrix_h_
#define matrix_h_

#include "common.h"
#include "value.h"

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
