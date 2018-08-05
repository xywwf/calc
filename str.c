#include "str.h"

#include <string.h>

Str *
str_new(const char *buf, size_t nbuf)
{
    Str *s = ls_xmalloc(sizeof(Str) + nbuf, 1);
    s->gchdr.nrefs = 1;
    s->ndata = nbuf;
    if (nbuf) {
        memcpy(s->data, buf, nbuf);
    }
    return s;
}

bool
str_eq(Str *a, Str *b)
{
    return
        a->ndata == b->ndata &&
        a->ndata &&
        memcmp(a->data, b->data, a->ndata) == 0;
}
