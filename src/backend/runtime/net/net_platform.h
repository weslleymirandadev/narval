// net_platform.h — the one place where POSIX sockets and Winsock2 differ.
//
// The runtime is compiled by gcc on Linux and by MinGW's gcc on Windows (see
// src/backend/runtime/CMakeLists.txt), so this header only has to cover those two. It is
// included FIRST by the net sources: winsock2.h must come before windows.h, and nothing
// here pulls windows.h in.
//
// What it unifies:
//   * the socket type and the close call          (int/SOCKET, close/closesocket)
//   * the error number and its message            (errno/WSAGetLastError, strerror/FormatMessage)
//   * the "would block" code                      (EAGAIN/EINPROGRESS vs WSAEWOULDBLOCK/WSAEINPROGRESS)
//   * the readiness call                          (poll vs WSAPoll)
//   * socket timeouts                             (struct timeval vs DWORD in MILLISECONDS)
#ifndef NV_NET_PLATFORM_H
#define NV_NET_PLATFORM_H

// WSAPoll and getaddrinfo need Vista+; the runtime targets modern Windows.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#ifdef _WIN32

#include <stdio.h>   // snprintf, in the error formatter below
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <errno.h>   // EINTR in the read loops

typedef SOCKET nv_sock_t;
#define NV_INVALID_SOCK INVALID_SOCKET
#define nv_sock_close(fd) closesocket(fd)
typedef int nv_socklen_t;

// WSAPoll wants ULONG; keep the POSIX signature so callers do not care.
#define nv_poll(fds, n, timeout_ms) WSAPoll((fds), (ULONG)(n), (INT)(timeout_ms))

typedef struct pollfd nv_pollfd_t;

// One WSAStartup for the process; a second call is harmless.
static inline int nv_net_platform_init(void) {
    static int started = 0;
    static int ok = 0;
    if (started) return ok;
    started = 1;
    WSADATA wsa;
    ok = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) ? 1 : 0;
    return ok;
}

static inline int nv_net_platform_errno(void) { return (int)WSAGetLastError(); }

static inline int nv_net_platform_would_block(int err) {
    return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS || err == WSAEALREADY;
}

static inline int nv_net_platform_in_progress(int err) {
    return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS || err == WSAEALREADY;
}

// WSAGetLastError codes have no strerror: ask the system for the text.
static inline const char* nv_net_platform_strerror(int err, char* buf, int buflen) {
    DWORD n = (DWORD)FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, (DWORD)err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, (DWORD)buflen, NULL);
    if (n == 0) {
        snprintf(buf, (size_t)buflen, "winsock error %d", err);
        return buf;
    }
    while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n' || buf[n - 1] == '.')) buf[--n] = 0;
    return buf;
}

// Winsock takes a DWORD in MILLISECONDS where POSIX takes a struct timeval. Passing a
// timeval on Windows is the bug that made every ICSP handshake "timed out" (the first four
// bytes of {3,0} are the number 3, i.e. a 3 ms timeout).
static inline void nv_net_set_sock_timeout(nv_sock_t fd, int optname, int ms) {
    DWORD v = (DWORD)ms;
    setsockopt(fd, SOL_SOCKET, optname, (const char*)&v, sizeof(v));
}

// Non-blocking mode is a different call on each side (ioctlsocket vs fcntl).
static inline int nv_net_set_nonblocking(nv_sock_t fd, int on) {
    u_long mode = on ? 1 : 0;
    return ioctlsocket(fd, FIONBIO, &mode) == 0 ? 0 : -1;
}

static inline int nv_net_platform_errno_of(int err) { return err; }

#else  // ── POSIX ────────────────────────────────────────────────────────────────

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

typedef int nv_sock_t;
#define NV_INVALID_SOCK (-1)
#define nv_sock_close(fd) close(fd)
typedef socklen_t nv_socklen_t;

#define nv_poll(fds, n, timeout_ms) poll((fds), (nfds_t)(n), (timeout_ms))

typedef struct pollfd nv_pollfd_t;

static inline int nv_net_platform_init(void) { return 1; }   // nothing to start
static inline int nv_net_platform_errno(void) { return errno; }

static inline int nv_net_platform_would_block(int err) {
    return err == EAGAIN || err == EWOULDBLOCK || err == EINTR;
}

static inline int nv_net_platform_in_progress(int err) {
    return err == EINPROGRESS || err == EAGAIN || err == EWOULDBLOCK || err == EINTR;
}

static inline const char* nv_net_platform_strerror(int err, char* buf, int buflen) {
    (void)buf;
    (void)buflen;
    return strerror(err);
}

static inline void nv_net_set_sock_timeout(nv_sock_t fd, int optname, int ms) {
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, optname, &tv, sizeof(tv));
}

static inline int nv_net_set_nonblocking(nv_sock_t fd, int on) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (on) flags |= O_NONBLOCK;
    else flags &= ~O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags) == 0 ? 0 : -1;
}

#endif

#endif  // NV_NET_PLATFORM_H
