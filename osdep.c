#include "osdep.h"

#ifdef __MINGW32__
#   include <windows.h>
#   include <ntstatus.h>

int OSDEP_UTF8_READY = 0;

bool
osdep_is_interactive(void)
{
    HANDLE h_in = GetStdHandle(STD_INPUT_HANDLE);
    if (h_in == INVALID_HANDLE_VALUE) {
        return false;
    }
    if (GetFileType(h_in) != FILE_TYPE_CHAR) {
        return false;
    }
    DWORD ignored;
    if (GetConsoleMode(h_in, &ignored) != 0) {
        return true;
    }
    return false;
}

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

int OSDEP_UTF8_READY = 1;

bool
osdep_is_interactive(void)
{
    if (!isatty(0)) {
        return false;
    }
    const char *term = getenv("TERM");
    if (!term || strcmp(term, "") == 0 || strcmp(term, "dumb") == 0) {
        return false;
    }
    return true;
}

void *
osdep_rng_new(void)
{
    int *r = XNEW(int, 1);
    if ((*r = open("/dev/urandom", O_RDONLY)) < 0) {
        free(r);
        return NULL;
    }
    return r;
}

bool
osdep_rng_fill(void *handle, void *buf, size_t nbuf)
{
    for (size_t nread = 0; nread != nbuf;) {
        const ssize_t r = read(*(int *) handle, (char *) buf + nread, nbuf - nread);
        if (r <= 0) {
            return false;
        }
        nread += r;
    }
    return true;
}

void
osdep_rng_destroy(void *handle)
{
    close(*(int *) handle);
    free(handle);
}

#endif
