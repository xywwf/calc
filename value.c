#include "value.h"
#include "env.h"

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
    Matrix *m = ls_xmalloc(sizeof(Matrix) + nelems * sizeof(Scalar), 1);
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
