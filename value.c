#include "value.h"
#include "matrix.h"
#include "str.h"
#include "func.h"

#include <stdio.h>

void
gcobject_destroy(Value v)
{
    switch (v.kind) {
    case VAL_KIND_FUNC:
        func_destroy((Func *) v.as.gcobj);
        break;
    default:
        break;
    }
    // Since this function is called, /v/ *is* a garbage-collected object.
    free(v.as.gcobj);
}

void
value_print(Value v)
{
    switch (v.kind) {
    case VAL_KIND_NIL:
        /* do not print anything */
        break;
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
        printf("<built-in function %p>\n", *(void **) &v.as.cfunc);
        break;
    case VAL_KIND_FUNC:
        printf("<function %p>\n", (void *) v.as.gcobj);
        break;
    case VAL_KIND_STR:
        {
            Str *s = (Str *) v.as.gcobj;
            fwrite(s->data, 1, s->ndata, stdout);
            fputc('\n', stdout);
        }
        break;
    }
}

bool
value_is_truthy(Value v)
{
    switch (v.kind) {
    case VAL_KIND_NIL:
        return false;
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
    case VAL_KIND_FUNC:
    case VAL_KIND_STR:
        return true;
    }
    LS_UNREACHABLE();
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
