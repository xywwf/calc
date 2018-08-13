#ifndef ls_string_h_
#define ls_string_h_

#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>

#include "vector.h"
#include "compdep.h"

// /LSString/ is a string defined as /LS_VECTOR_OF(char)/. It is guaranteed to be defined in this
// way, so that all the /LS_VECTOR_*/ macros work with /LSString/s.
typedef LS_VECTOR_OF(char) LSString;

// Assigns a string value to /s/. If /nbuf/ is /0/, /buf/ is not required to be dereferencable.
LS_INHEADER
void
ls_string_assign_b(LSString *s, const char *buf, size_t nbuf)
{
    LS_VECTOR_ENSURE(*s, nbuf);
    // see DOCS/c_notes/empty-ranges-and-c-stdlib.md
    if (nbuf) {
        memcpy(s->data, buf, nbuf);
    }
    s->size = nbuf;
}

// Assigns a zero-terminated value to /s/.
LS_INHEADER
void
ls_string_assign_s(LSString *s, const char *cstr)
{
    ls_string_assign_b(s, cstr, strlen(cstr));
}

// Assigns a char value to /s/.
LS_INHEADER
void
ls_string_assign_c(LSString *s, char c)
{
    ls_string_assign_b(s, &c, 1);
}

// Appends a string to /s/. If /nbuf/ is /0/, /buf/ is not required to be dereferencable.
LS_INHEADER
void
ls_string_append_b(LSString *s, const char *buf, size_t nbuf)
{
    LS_VECTOR_ENSURE(*s, s->size + nbuf);
    // see DOCS/c_notes/empty-ranges-and-c-stdlib.md
    if (nbuf) {
        memcpy(s->data + s->size, buf, nbuf);
    }
    s->size += nbuf;
}

// Appends a zero-terminated string to /s/.
LS_INHEADER
void
ls_string_append_s(LSString *s, const char *cstr)
{
    ls_string_append_b(s, cstr, strlen(cstr));
}

// Appends a char to /s/.
LS_INHEADER
void
ls_string_append_c(LSString *s, char c)
{
    LS_VECTOR_PUSH(*s, c);
}

// Constructs a new /LSString/ initialized with a value of the zero-terminated string /cstr/.
LS_INHEADER
LSString
ls_string_new_from_s(const char *cstr)
{
    LSString r = LS_VECTOR_NEW();
    ls_string_assign_s(&r, cstr);
    return r;
}

// Constructs a new /LSString/ initialized with a value of the buffer given. If /nbuf/ is /0/, /buf/
// is not required to be dereferencable.
LS_INHEADER
LSString
ls_string_new_from_b(const char *buf, size_t nbuf)
{
    LSString r = LS_VECTOR_NEW();
    ls_string_assign_b(&r, buf, nbuf);
    return r;
}

// Constructs a new /LSString/ initialized with a value of the char given.
LS_INHEADER
LSString
ls_string_new_from_c(char c)
{
    LSString r = LS_VECTOR_NEW();
    LS_VECTOR_PUSH(r, c);
    return r;
}

#endif
