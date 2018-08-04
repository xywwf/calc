#include "value.h"
#include "env.h"

#include <stdio.h>

void
value_print(Value v)
{
    switch (v.kind) {
    case VAL_KIND_SCALAR:
        printf("%.15g\n", v.as.scalar);
        break;
    case VAL_KIND_MATRIX:
        {
            Matrix *m = (Matrix *) v.as.gcobj;
            size_t elem_idx = 0;
            puts("[");
            for (unsigned i = 0; i < m->height; ++i) {
                for (unsigned j = 0; j < m->width; ++j) {
                    printf("\t%.15g", m->elems[elem_idx++]);
                }
                puts("");
            }
            puts("]");
        }
        break;
    case VAL_KIND_CFUNC:
        printf("<built-in function>\n");
        break;
    }
}

bool
value_is_truthy(Value v)
{
    switch (v.kind) {
    case VAL_KIND_SCALAR:
        return !!v.as.scalar;
    case VAL_KIND_MATRIX:
        {
            Matrix *m = (Matrix *) v.as.gcobj;
            const size_t nelems = (size_t) m->height * m->width;
            for (size_t i = 0; i < nelems; ++i) {
                if (m->elems[i]) {
                    return true;
                }
            }
            return false;
        }
        break;
    case VAL_KIND_CFUNC:
        return true;
    default:
        LS_UNREACHABLE();
    }
}

bool
scalar_parse(const char *buf, size_t nbuf, Scalar *result)
{
    Scalar r = 0;
    Scalar f = 1;

    size_t i = 0;
    for (; i < nbuf; ++i) {
        if (buf[i] == '.') {
            break;
        }
        r *= 10;
        r += buf[i] - '0';
    }
    ++i;
    for (; i < nbuf; ++i) {
        if (buf[i] == '.') {
            return false;
        }
        f /= 10;
        r += f * (buf[i] - '0');
    }

    *result = r;
    return true;
}

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
        m->elems[i] = elems[i].as.scalar;
    }
    return m;
}

Value
matrix_get1(Env *e, Matrix *m, Value elem)
{
    if (elem.kind != VAL_KIND_SCALAR) {
        env_throw(e, "cannot index matrix with %s value", value_kindname(elem.kind));
    }
    const size_t num = elem.as.scalar;
    if (num < 1 || num > (size_t) m->width * m->height) {
        env_throw(e, "element number out of range");
    }

    return (Value) {.kind = VAL_KIND_SCALAR, .as = {.scalar = m->elems[num - 1]}};
}

Value
matrix_get2(Env *e, Matrix *m, Value row, Value col)
{
    if (row.kind != VAL_KIND_SCALAR || col.kind != VAL_KIND_SCALAR) {
        env_throw(e, "cannot index matrix with (%s, %s) values",
                  value_kindname(row.kind), value_kindname(col.kind));
    }
    const size_t i = row.as.scalar;
    const size_t j = col.as.scalar;
    if (i < 1 || i > m->height) {
        env_throw(e, "row number out of range");
    }
    if (j < 1 || j > m->width) {
        env_throw(e, "column number out of range");
    }
    const size_t index = (i - 1) * m->width + (j - 1);
    return (Value) {.kind = VAL_KIND_SCALAR, .as = {.scalar = m->elems[index]}};
}

void
matrix_set1(Env *e, Matrix *m, Value elem, Value v)
{
    if (elem.kind != VAL_KIND_SCALAR) {
        env_throw(e, "cannot index matrix with %s value", value_kindname(elem.kind));
    }
    const size_t num = elem.as.scalar;
    if (num < 1 || num > (size_t) m->width * m->height) {
        env_throw(e, "element number out of range");
    }

    if (v.kind != VAL_KIND_SCALAR) {
        env_throw(e, "cannot assign matrix element a %s value", value_kindname(v.kind));
    }
    m->elems[num - 1] = v.as.scalar;
}

void
matrix_set2(Env *e, Matrix *m, Value row, Value col, Value v)
{
    if (row.kind != VAL_KIND_SCALAR || col.kind != VAL_KIND_SCALAR) {
        env_throw(e, "cannot index matrix with (%s, %s) pair",
                  value_kindname(row.kind), value_kindname(col.kind));
    }
    const size_t i = row.as.scalar;
    const size_t j = col.as.scalar;
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
    m->elems[index] = v.as.scalar;
}
