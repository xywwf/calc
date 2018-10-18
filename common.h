#ifndef common_h_
#define common_h_

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <setjmp.h>

#ifdef __GNUC__
#   define ATTR_UNUSED         __attribute__((unused))
#   define ATTR_NORETURN       __attribute__((noreturn))
#   define ATTR_PRINTF(N_, M_) __attribute__((format(printf, N_, M_)))
#   define UNREACHABLE()       __builtin_unreachable()
#else
#   define ATTR_UNUSED
#   define ATTR_NORETURN
#   define ATTR_PRINTF(N_, M_)
#   define UNREACHABLE()       abort()
#endif

#define PANIC__CAT(X_) #X_

// ...because /PANIC__CAT(__LINE__)/ would give "__LINE__".
#define PANIC__EVAL_CAT(X_) PANIC__CAT(X_)

#define PANIC(Msg_) \
    do { \
        fputs("PANIC at " __FILE__ ":" PANIC__EVAL_CAT(__LINE__) ": " Msg_ "\n", \
              stderr); \
        abort(); \
    } while (0)

#define INHEADER static inline ATTR_UNUSED

#define XNEW(Type_, NElems_)  ((Type_ *) xmalloc(NElems_, sizeof(Type_)))

#define XNEW0(Type_, NElems_) ((Type_ *) xcalloc(NElems_, sizeof(Type_)))

#define oom_handler() PANIC("out of memory")

INHEADER
void *
xmalloc(size_t nelems, size_t elemsz)
{
    if (elemsz && nelems > SIZE_MAX / elemsz) {
        goto oom;
    }
    const size_t n = nelems * elemsz;
    void *r = malloc(n);
    if (n && !r) {
        goto oom;
    }
    return r;

oom:
    oom_handler();
}

INHEADER
void *
xcalloc(size_t nelems, size_t elemsz)
{
    void *r = calloc(nelems, elemsz);
    if (nelems && elemsz && !r) {
        oom_handler();
    }
    return r;
}

INHEADER
void *
xrealloc(void *p, size_t nelems, size_t elemsz)
{
    if (elemsz && nelems > SIZE_MAX / elemsz) {
        goto oom;
    }
    const size_t n = nelems * elemsz;
    void *r = realloc(p, n);
    if (n && !r) {
        goto oom;
    }
    return r;

oom:
    oom_handler();
}

INHEADER
void *
x2realloc(void *p, size_t *pnelems, size_t elemsz)
{
    const size_t oldnelems = *pnelems;

    size_t newnelems;
    size_t n;

    if (oldnelems) {
        if (elemsz && oldnelems > SIZE_MAX / 2 / elemsz) {
            goto oom;
        }
        newnelems = oldnelems * 2;
        n = newnelems * elemsz;
    } else {
        newnelems = 1;
        n = elemsz;
    }

    void *r = realloc(p, n);
    if (n && !r) {
        goto oom;
    }
    *pnelems = newnelems;
    return r;

oom:
    oom_handler();
}

INHEADER
void *
x2realloc0(void *p, size_t *pnelems, size_t elemsz)
{
    const size_t oldnelems = *pnelems;
    p = x2realloc(p, pnelems, elemsz);
    memset((char *) p + elemsz * oldnelems, 0, elemsz * (*pnelems - oldnelems));
    return p;
}

INHEADER
void *
xmemdup(const void *p, size_t n)
{
    void *r = malloc(n);
    if (n) {
        if (!r) {
            oom_handler();
        }
        memcpy(r, p, n);
    }
    return r;
}

INHEADER
char *
xstrdup(const char *s)
{
    char *r = strdup(s);
    if (!r) {
        oom_handler();
    }
    return r;
}

#endif
