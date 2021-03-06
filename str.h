#ifndef str_h_
#define str_h_

#include "common.h"
#include "value.h"

typedef struct {
    GcObject gchdr;
    size_t ndata;
    char data[];
} Str;

Str *
str_new(const char *buf, size_t nbuf);

Str *
str_new_unescape(const char *buf, size_t nbuf);

Str *
str_new_concat(const char *a, size_t na, const char *b, size_t nb);

bool
str_eq(Str *a, Str *b);

#endif
