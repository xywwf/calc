#include "string_.h"

#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>

#include "vector.h"

bool
ls_string_append_vf(LSString *s, const char *fmt, va_list vl)
{
    va_list vl2;
    va_copy(vl2, vl);
    bool ret = false;
    int saved_errno;

    size_t navail = s->capacity - s->size;
    int r = vsnprintf(s->data + s->size, navail, fmt, vl);
    if (r < 0) {
        goto cleanup;
    }
    if ((size_t) r >= navail) {
        LS_VECTOR_ENSURE(*s, s->size + r + 1);
        int r2 = vsnprintf(s->data + s->size, (size_t) r + 1, fmt, vl2);
        (void) r2;
        assert(r2 == r);
    }
    s->size += (size_t) r;
    ret = true;

cleanup:
    saved_errno = errno;
    va_end(vl2);
    errno = saved_errno;
    return ret;
}
