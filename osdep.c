#include "osdep.h"

#ifdef __MINGW32__
#   include <windows.h>

void *
osdep_rng_new(void)
{
    return ""; // definitely not NULL
}

bool
osdep_rng_fill(void *handle, void *buf, size_t nbuf)
{
    (void) handle;
    return BCryptGenRandom(NULL, (PUCHAR) buf, nbuf, BCRYPT_USE_SYSTEM_PREFERRED_RNG)
            == STATUS_SUCCESS;
}

void
osdep_rng_destroy(void *handle)
{
    (void) handle;
}

#else
#   include <unistd.h>
#   include <fcntl.h>

void *
osdep_rng_new(void)
{
    int *r = LS_XNEW(int, 1);
    if ((*r = open("/dev/urandom", O_RDONLY)) < 0) {
        free(r);
        return NULL;
    }
    return r;
}

bool
osdep_rng_fill(void *handle, void *buf, size_t nbuf)
{
    return read(*(int *) handle, buf, nbuf) == (ssize_t) nbuf;
}

void
osdep_rng_destroy(void *handle)
{
    free(handle);
}
#endif
