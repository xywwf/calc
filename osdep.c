#include "osdep.h"

#ifdef __MINGW32__
#   include <windows.h>
#   include <errno.h>

int
osdep_fill_random(void *buf, size_t nbuf)
{
    if (BCryptGenRandom(NULL, (PUCHAR) buf, nbuf, BCRYPT_USE_SYSTEM_PREFERRED_RNG)
            != STATUS_SUCCESS)
    {
        errno = EINVAL;
        return -1;
    }
    return 0;
}
#else
#   include <unistd.h>
#   include <fcntl.h>
int
osdep_fill_random(void *buf, size_t nbuf)
{
    // Not thread-safe. Nobody cares.
    static int fd = -1;
    if (fd == -1) {
        fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0) {
            return -1;
        }
    }
    if (read(fd, buf, nbuf) != (ssize_t) nbuf) {
        return -1;
    }
    return 0;
}
#endif
