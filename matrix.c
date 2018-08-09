#include "matrix.h"
#include "env.h"

Matrix *
matrix_new(unsigned height, unsigned width)
{
    const size_t nelems = height * width;
    Matrix *m = ls_xcalloc(sizeof(Matrix) + nelems * sizeof(Scalar), 1);
    m->gchdr.nrefs = 1;
    m->height = height;
    m->width = width;
    return m;
}

Matrix *
matrix_construct(Env *e, const Value *elems, unsigned height, unsigned width)
{
    Matrix *m = matrix_new(height, width);
    const size_t nelems = height * width;
    for (size_t i = 0; i < nelems; ++i) {
        if (elems[i].kind != VAL_KIND_SCALAR) {
            free(m);
            env_throw(e, "matrix element is %s (scalar expected)", value_kindname(elems[i].kind));
        }
        m->elems[i] = AS_SCL(elems[i]);
    }
    return m;
}

Value
matrix_get1(Env *e, Matrix *m, Value elem)
{
    if (elem.kind != VAL_KIND_SCALAR) {
        env_throw(e, "cannot index matrix with %s value", value_kindname(elem.kind));
    }
    const size_t num = AS_SCL(elem);
    if (num < 1 || num > (size_t) m->width * m->height) {
        env_throw(e, "element number out of range");
    }

    return MK_SCL(m->elems[num - 1]);
}

Value
matrix_get2(Env *e, Matrix *m, Value row, Value col)
{
    if (row.kind != VAL_KIND_SCALAR || col.kind != VAL_KIND_SCALAR) {
        env_throw(e, "cannot index matrix with (%s, %s) values",
                  value_kindname(row.kind), value_kindname(col.kind));
    }
    const size_t i = AS_SCL(row);
    const size_t j = AS_SCL(col);
    if (i < 1 || i > m->height) {
        env_throw(e, "row number out of range");
    }
    if (j < 1 || j > m->width) {
        env_throw(e, "column number out of range");
    }
    const size_t index = (i - 1) * m->width + (j - 1);

    return MK_SCL(m->elems[index]);
}

void
matrix_set1(Env *e, Matrix *m, Value elem, Value v)
{
    if (elem.kind != VAL_KIND_SCALAR) {
        env_throw(e, "cannot index matrix with %s value", value_kindname(elem.kind));
    }
    const size_t num = AS_SCL(elem);
    if (num < 1 || num > (size_t) m->width * m->height) {
        env_throw(e, "element number out of range");
    }

    if (v.kind != VAL_KIND_SCALAR) {
        env_throw(e, "cannot assign matrix element a %s value", value_kindname(v.kind));
    }
    m->elems[num - 1] = AS_SCL(v);
}

void
matrix_set2(Env *e, Matrix *m, Value row, Value col, Value v)
{
    if (row.kind != VAL_KIND_SCALAR || col.kind != VAL_KIND_SCALAR) {
        env_throw(e, "cannot index matrix with (%s, %s) pair",
                  value_kindname(row.kind), value_kindname(col.kind));
    }
    const size_t i = AS_SCL(row);
    const size_t j = AS_SCL(col);
    if (i < 1 || i > m->height) {
        env_throw(e, "row number out of range");
    }
    if (j < 1 || j > m->width) {
        env_throw(e, "column number out of range");
    }
    const size_t index = (i - 1) * m->width + (j - 1);

    if (v.kind != VAL_KIND_SCALAR) {
        env_throw(e, "cannot assign matrix element a %s value", value_kindname(v.kind));
    }
    m->elems[index] = AS_SCL(v);
}
