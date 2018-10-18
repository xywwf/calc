#include "str.h"

Str *
str_new(const char *buf, size_t nbuf)
{
    Str *s = xmalloc(sizeof(Str) + nbuf, 1);
    s->gchdr.nrefs = 1;
    s->ndata = nbuf;
    if (nbuf) {
        memcpy(s->data, buf, nbuf);
    }
    return s;
}

Str *
str_new_unescape(const char *buf, size_t nbuf)
{
    Str *s = xmalloc(sizeof(Str) + nbuf, 1);
    s->gchdr.nrefs = 1;

    char *ptr = s->data;
    for (const char *t; nbuf && (t = memchr(buf, '\\', nbuf));) {
        const size_t nseg = t - buf;
        if (nseg) {
            memcpy(ptr, buf, nseg);
            ptr += nseg;
        }
        buf += nseg + 1;
        nbuf -= nseg + 1;
        if (!nbuf) {
            break;
        }
        switch (*buf) {
            case 'n':  *ptr++ = '\n'; break;
            case 'q':  *ptr++ = '"';  break;
            case '\\': *ptr++ = '\\'; break;
        }
        ++buf;
        --nbuf;
    }
    if (nbuf) {
        memcpy(ptr, buf, nbuf);
        ptr += nbuf;
    }

    s->ndata = ptr - s->data;
    return xrealloc(s, sizeof(Str) + s->ndata, 1);
}

Str *
str_new_concat(const char *a, size_t na, const char *b, size_t nb)
{
    Str *s = xmalloc(sizeof(Str) + na + nb, 1);
    s->gchdr.nrefs = 1;
    s->ndata = na + nb;
    if (na) {
        memcpy(s->data, a, na);
    }
    if (nb) {
        memcpy(s->data + na, b, nb);
    }
    return s;
}

bool
str_eq(Str *a, Str *b)
{
    return a->ndata == b->ndata && a->ndata && memcmp(a->data, b->data, a->ndata) == 0;
}
