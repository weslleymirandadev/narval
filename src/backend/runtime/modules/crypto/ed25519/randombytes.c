/* randombytes.c - secure RNG backend for TweetNaCl.
 * POSIX: /dev/urandom; Windows: BCryptGenRandom. Same interface.
 */
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "tweetnacl.h"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>

void randombytes(uint8_t *buf, uint64_t n)
{
    while (n > 0) {
        ULONG chunk = (ULONG)(n > 0xFFFFFFFFULL ? 0xFFFFFFFFULL : n);
        if (BCryptGenRandom(NULL, buf, chunk,
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
            continue;
        buf += chunk;
        n -= chunk;
    }
}
#else
/* /dev/urandom instead of getrandom(): the getrandom() prototype is
 * gated behind _GNU_SOURCE on glibc and __ANDROID_API__ >= 28 on
 * bionic (Termux builds with 24), which clang >= 16 turns into an
 * implicit-declaration error. /dev/urandom exists on every POSIX
 * target (Linux, Android, WSL, chroots). */
void randombytes(uint8_t *buf, uint64_t n)
{
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        abort();                /* no OS randomness: never emit weak keys */
    while (n > 0) {
        ssize_t r = read(fd, buf, (size_t)n);
        if (r <= 0) {
            if (r < 0 && errno == EINTR)
                continue;
            abort();
        }
        buf += r;
        n -= (uint64_t)r;
    }
    close(fd);
}
#endif
