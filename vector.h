#ifndef vector_h_
#define vector_h_

#include "common.h"

#define VECTOR_OF(Type_) \
    struct { \
        Type_ *data; \
        size_t size; \
        size_t capacity; \
    }

#define VECTOR_NEW() {NULL, 0, 0}

#define VECTOR_INIT(Vec_) \
    do { \
        (Vec_).data = NULL; \
        (Vec_).size = 0; \
        (Vec_).capacity = 0; \
    } while (0)

#define VECTOR_ENSURE(Vec_, N_) \
    do { \
        while ((Vec_).capacity < (N_)) { \
            (Vec_).data = x2realloc((Vec_).data, &(Vec_).capacity, sizeof((Vec_).data[0])); \
        } \
    } while (0)

#define VECTOR_PUSH(Vec_, Elem_) \
    do { \
        if ((Vec_).size == (Vec_).capacity) { \
            (Vec_).data = x2realloc((Vec_).data, &(Vec_).capacity, sizeof((Vec_).data[0])); \
        } \
        (Vec_).data[(Vec_).size++] = (Elem_); \
    } while (0)

#define VECTOR_CLEAR(Vec_) \
    do { \
        (Vec_).size = 0; \
    } while (0)

#define VECTOR_POP(Vec_) \
    ((Vec_).data[--(Vec_).size])

#define VECTOR_SHRINK(Vec_) \
    do { \
        (Vec_).data = xrealloc((Vec_).data, (Vec_).size, sizeof((Vec_).data[0])); \
    } while (0)

#define VECTOR_FREE(Vec_) free((Vec_).data)

typedef VECTOR_OF(char) CharVector;

INHEADER
void
char_vector_append(CharVector *s, const char *buf, size_t nbuf)
{
    if (!nbuf) {
        return;
    }
    VECTOR_ENSURE(*s, s->size + nbuf);
    memcpy(s->data + s->size, buf, nbuf);
    s->size += nbuf;
}

#endif
