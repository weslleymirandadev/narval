// net_socket.c — the portable socket layer under Narval's `net` stdlib.
//
// A socket is a small integer: the same model as File and Sqlite, because that is what fits
// through the runtime's value model. The table is fixed and small (nothing in a script needs
// hundreds of live sockets at once) and a closed handle is immediately reusable.
//
// Every entry point sets the handle's status and the process-wide error text, so a caller
// never reads errno: an empty string plus nv_net_status() is the whole story
// (OK / TIMEOUT / CLOSED / ERROR). Timeouts are always milliseconds — see net_platform.h for
// why that matters on Windows.
//
// Results are returned in one growable heap buffer (never a big stack array: Windows gives a
// thread 1 MB and the BAIT layer above this one moves 256 KiB messages).

#include "net_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend/runtime/net_bridge.h"

#define NV_NET_MAX 64
#define NV_NET_ADDR_MAX 128
#define NV_NET_PEND_MAX 4096

enum { NV_NET_FREE = 0, NV_NET_LISTENER = 1, NV_NET_STREAM = 2, NV_NET_UDP = 3 };

typedef struct {
    nv_sock_t fd;
    int kind;
    int status;
    int nonblocking;
    int last_len;
    int pend_len;
    char peer[NV_NET_ADDR_MAX];
    char pend[NV_NET_PEND_MAX];
} nv_net_slot;

static nv_net_slot g_slots[NV_NET_MAX];
static char g_error[256] = "";
static int g_last_len = 0;
static int g_started = 0;
static char* g_buf = NULL;
static size_t g_buf_cap = 0;

// ── plumbing ─────────────────────────────────────────────────────────────────

void nv_net_init(void) {
    if (g_started) return;
    g_started = 1;
    if (!nv_net_platform_init()) {
        snprintf(g_error, sizeof(g_error), "socket layer did not start on this platform");
        return;
    }
    for (int i = 0; i < NV_NET_MAX; ++i) g_slots[i].fd = NV_INVALID_SOCK;
}

int nv_net_ready(void) { return g_started && nv_net_platform_init(); }

static nv_net_slot* slot_of(int handle) {
    if (handle < 0 || handle >= NV_NET_MAX) return NULL;
    if (g_slots[handle].kind == NV_NET_FREE) return NULL;
    return &g_slots[handle];
}

static void set_error(int err) {
    char tmp[192];
    snprintf(g_error, sizeof(g_error), "%s", nv_net_platform_strerror(err, tmp, (int)sizeof(tmp)));
}

static void set_error_text(const char* text) { snprintf(g_error, sizeof(g_error), "%s", text); }

static char* reserve(size_t need) {
    if (g_buf_cap >= need) return g_buf;
    size_t next = g_buf_cap ? g_buf_cap : 512;
    while (next < need) next *= 2;
    char* grown = (char*)realloc(g_buf, next);
    if (!grown) return NULL;
    g_buf = grown;
    g_buf_cap = next;
    return g_buf;
}

static int alloc_slot(nv_sock_t fd, int kind) {
    if (fd == NV_INVALID_SOCK) return -1;
    for (int i = 0; i < NV_NET_MAX; ++i) {
        if (g_slots[i].kind != NV_NET_FREE) continue;
        nv_net_slot* s = &g_slots[i];
        memset(s, 0, sizeof(*s));
        s->fd = fd;
        s->kind = kind;
        s->status = NV_NET_OK;
        return i;
    }
    set_error_text("too many open sockets");
    nv_sock_close(fd);
    return -1;
}

// "1.2.3.4:80" and "[::1]:80" — one shape, so a Narval string carries an endpoint.
static void format_addr(const struct sockaddr* sa, nv_socklen_t len, char* out, size_t outlen) {
    char host[128] = "";
    char serv[32] = "";
    if (getnameinfo(sa, len, host, (nv_socklen_t)sizeof(host), serv, (nv_socklen_t)sizeof(serv),
                    NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
        snprintf(out, outlen, "unknown");
        return;
    }
    if (sa->sa_family == AF_INET6)
        snprintf(out, outlen, "[%s]:%s", host, serv);
    else
        snprintf(out, outlen, "%s:%s", host, serv);
}

// Resolves host:port once and gives back the first usable sockaddr. kind: SOCK_STREAM or
// SOCK_DGRAM; passive = this side is binding (host "" = every interface).
static int resolve_endpoint(const char* host, int port, int kind, int passive,
                            struct sockaddr_storage* out, nv_socklen_t* out_len) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = kind;
    hints.ai_flags = passive ? AI_PASSIVE : 0;
    char service[16];
    snprintf(service, sizeof(service), "%d", port);
    struct addrinfo* res = NULL;
    int rc = getaddrinfo((host && host[0]) ? host : NULL, service, &hints, &res);
    if (rc != 0 || !res) {
        snprintf(g_error, sizeof(g_error), "%s: %s",
                 (host && host[0]) ? host : "*", "cannot resolve address");
        return -1;
    }
    memcpy(out, res->ai_addr, res->ai_addrlen);
    *out_len = (nv_socklen_t)res->ai_addrlen;
    freeaddrinfo(res);
    return 0;
}

static int create_bound(const char* host, int port, int kind, int do_listen, int* out_handle) {
    nv_net_init();
    struct sockaddr_storage addr;
    nv_socklen_t addr_len = 0;
    if (resolve_endpoint(host, port, kind, 1, &addr, &addr_len) != 0) return -1;
    nv_sock_t fd = socket(addr.ss_family, kind, 0);
    if (fd == NV_INVALID_SOCK) {
        set_error(nv_net_platform_errno());
        return -1;
    }
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));
    if (bind(fd, (struct sockaddr*)&addr, addr_len) != 0) {
        set_error(nv_net_platform_errno());
        nv_sock_close(fd);
        return -1;
    }
    if (do_listen && listen(fd, 64) != 0) {
        set_error(nv_net_platform_errno());
        nv_sock_close(fd);
        return -1;
    }
    int handle = alloc_slot(fd, do_listen ? NV_NET_LISTENER
                                          : (kind == SOCK_DGRAM ? NV_NET_UDP : NV_NET_STREAM));
    if (handle < 0) return -1;
    *out_handle = handle;
    return 0;
}

// Waits for readiness; 1 ready, 0 timed out, -1 error.
static int wait_ready(nv_sock_t fd, int write_side, int timeout_ms) {
    nv_pollfd_t p;
    p.fd = fd;
    p.events = (short)(write_side ? POLLOUT : POLLIN);
    p.revents = 0;
    for (;;) {
        int rc = nv_poll(&p, 1, timeout_ms);
        if (rc > 0) return 1;
        if (rc == 0) return 0;
        int err = nv_net_platform_errno();
        if (err == EINTR) continue;
        if (nv_net_platform_would_block(err)) continue;
        set_error(err);
        return -1;
    }
}

// ── TCP ─────────────────────────────────────────────────────────────────────

int nv_net_tcp_listen(const char* host, int port) {
    int handle = -1;
    if (create_bound(host, port, SOCK_STREAM, 1, &handle) != 0) return -1;
    return handle;
}

int nv_net_tcp_connect(const char* host, int port, int timeout_ms) {
    nv_net_init();
    struct sockaddr_storage addr;
    nv_socklen_t addr_len = 0;
    if (resolve_endpoint(host, port, SOCK_STREAM, 0, &addr, &addr_len) != 0) return -1;
    nv_sock_t fd = socket(addr.ss_family, SOCK_STREAM, 0);
    if (fd == NV_INVALID_SOCK) {
        set_error(nv_net_platform_errno());
        return -1;
    }

    int rc;
    if (timeout_ms > 0) {
        // Non-blocking connect + a deadline: the portable way to time a connect out.
        if (nv_net_set_nonblocking(fd, 1) != 0) {
            set_error(nv_net_platform_errno());
            nv_sock_close(fd);
            return -1;
        }
        rc = connect(fd, (struct sockaddr*)&addr, addr_len);
        if (rc != 0) {
            int err = nv_net_platform_errno();
            if (!nv_net_platform_in_progress(err)) {
                set_error(err);
                nv_sock_close(fd);
                return -1;
            }
            int ready = wait_ready(fd, 1, timeout_ms);
            if (ready <= 0) {
                if (ready == 0) set_error_text("connect timed out");
                nv_sock_close(fd);
                return -1;
            }
            int so_err = 0;
            nv_socklen_t so_len = (nv_socklen_t)sizeof(so_err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&so_err, &so_len);
            if (so_err != 0) {
                set_error(so_err);
                nv_sock_close(fd);
                return -1;
            }
        }
        nv_net_set_nonblocking(fd, 0);
    } else {
        rc = connect(fd, (struct sockaddr*)&addr, addr_len);
        if (rc != 0) {
            set_error(nv_net_platform_errno());
            nv_sock_close(fd);
            return -1;
        }
    }

    int handle = alloc_slot(fd, NV_NET_STREAM);
    if (handle < 0) return -1;
    struct sockaddr_storage peer;
    nv_socklen_t peer_len = (nv_socklen_t)sizeof(peer);
    if (getpeername(fd, (struct sockaddr*)&peer, &peer_len) == 0)
        format_addr((struct sockaddr*)&peer, peer_len, g_slots[handle].peer, NV_NET_ADDR_MAX);
    return handle;
}

int nv_net_accept(int listener, int timeout_ms) {
    nv_net_slot* s = slot_of(listener);
    if (!s || s->kind != NV_NET_LISTENER) {
        set_error_text("not a listening socket");
        return -1;
    }
    if (timeout_ms > 0) {
        int ready = wait_ready(s->fd, 0, timeout_ms);
        if (ready == 0) {
            s->status = NV_NET_TIMEOUT;
            set_error_text("accept timed out");
            return NV_NET_TIMEOUT_HANDLE;
        }
        if (ready < 0) {
            s->status = NV_NET_ERROR;
            return -1;
        }
    }
    struct sockaddr_storage peer;
    nv_socklen_t peer_len = (nv_socklen_t)sizeof(peer);
    nv_sock_t fd = accept(s->fd, (struct sockaddr*)&peer, &peer_len);
    if (fd == NV_INVALID_SOCK) {
        int err = nv_net_platform_errno();
        if (nv_net_platform_would_block(err)) {
            s->status = NV_NET_TIMEOUT;
            set_error_text("no connection waiting");
            return NV_NET_TIMEOUT_HANDLE;
        }
        set_error(err);
        s->status = NV_NET_ERROR;
        return -1;
    }
    int handle = alloc_slot(fd, NV_NET_STREAM);
    if (handle < 0) {
        s->status = NV_NET_ERROR;
        return -1;
    }
    format_addr((struct sockaddr*)&peer, peer_len, g_slots[handle].peer, NV_NET_ADDR_MAX);
    s->status = NV_NET_OK;
    return handle;
}

// ── UDP ─────────────────────────────────────────────────────────────────────

int nv_net_udp_bind(const char* host, int port) {
    int handle = -1;
    if (create_bound(host, port, SOCK_DGRAM, 0, &handle) != 0) return -1;
    return handle;
}

int nv_net_udp_send_to(int handle, const char* host, int port, const char* data) {
    nv_net_slot* s = slot_of(handle);
    if (!s || s->kind != NV_NET_UDP || !data) {
        set_error_text("not a UDP socket");
        return -1;
    }
    struct sockaddr_storage addr;
    nv_socklen_t addr_len = 0;
    if (resolve_endpoint(host, port, SOCK_DGRAM, 0, &addr, &addr_len) != 0) {
        s->status = NV_NET_ERROR;
        return -1;
    }
    const size_t len = strlen(data);
    int sent = (int)sendto(s->fd, data, len, 0, (struct sockaddr*)&addr, addr_len);
    if (sent < 0) {
        set_error(nv_net_platform_errno());
        s->status = NV_NET_ERROR;
        return -1;
    }
    s->status = NV_NET_OK;
    return sent;
}

const char* nv_net_udp_recv_from(int handle, int max_bytes) {
    nv_net_slot* s = slot_of(handle);
    if (!s || s->kind != NV_NET_UDP) {
        set_error_text("not a UDP socket");
        return "";
    }
    if (max_bytes <= 0 || max_bytes > (1 << 20)) max_bytes = 65535;
    if (!reserve((size_t)max_bytes + NV_NET_ADDR_MAX + 8)) {
        set_error_text("out of memory");
        s->status = NV_NET_ERROR;
        return "";
    }
    const size_t cap = g_buf_cap;
    char* payload = g_buf + NV_NET_ADDR_MAX + 2;   // the address goes in front
    struct sockaddr_storage from;
    nv_socklen_t from_len = (nv_socklen_t)sizeof(from);
    int n = (int)recvfrom(s->fd, payload, cap - NV_NET_ADDR_MAX - 8, 0,
                          (struct sockaddr*)&from, &from_len);
    if (n < 0) {
        int err = nv_net_platform_errno();
        if (nv_net_platform_would_block(err)) {
            s->status = NV_NET_TIMEOUT;
            set_error_text("receive timed out");
        } else {
            s->status = NV_NET_ERROR;
            set_error(err);
        }
        return "";
    }
    g_buf[0] = '\0';
    format_addr((struct sockaddr*)&from, from_len, g_buf, NV_NET_ADDR_MAX);
    const size_t addr_len = strlen(g_buf);
    g_buf[addr_len] = '|';
    memmove(g_buf + addr_len + 1, payload, (size_t)n);
    g_buf[addr_len + 1 + (size_t)n] = '\0';
    s->status = NV_NET_OK;
    s->last_len = n;
    g_last_len = n;
    format_addr((struct sockaddr*)&from, from_len, s->peer, NV_NET_ADDR_MAX);
    return g_buf;
}

int nv_net_last_len(void) { return g_last_len; }

// ── streams ─────────────────────────────────────────────────────────────────

int nv_net_send_bytes(int handle, const char* data, int len) {
    nv_net_slot* s = slot_of(handle);
    if (!s || !data || len < 0) {
        set_error_text("not a connected socket");
        return -1;
    }
    int sent = 0;
    while (sent < len) {
        int n = (int)send(s->fd, data + sent, (size_t)(len - sent), 0);
        if (n > 0) {
            sent += n;
            continue;
        }
        int err = nv_net_platform_errno();
        if (n < 0 && nv_net_platform_would_block(err) && s->nonblocking) break;   // partial is fine
        if (n < 0 && err == EINTR) continue;
        set_error(err);
        s->status = NV_NET_ERROR;
        return sent > 0 ? sent : -1;
    }
    s->status = NV_NET_OK;
    return sent;
}

int nv_net_send(int handle, const char* data) {
    if (!data) return -1;
    return nv_net_send_bytes(handle, data, (int)strlen(data));
}

// Reads data from the handle: whatever was buffered (a previous read_until left it) first,
// else the socket. Returns the byte count, 0 on close, -1 on error/timeout.
static int read_some(nv_net_slot* s, char* dst, int want) {
    if (s->pend_len > 0) {
        int take = s->pend_len < want ? s->pend_len : want;
        memcpy(dst, s->pend, (size_t)take);
        memmove(s->pend, s->pend + take, (size_t)(s->pend_len - take));
        s->pend_len -= take;
        return take;
    }
    int n = (int)recv(s->fd, dst, (size_t)want, 0);
    if (n >= 0) return n;
    int err = nv_net_platform_errno();
    if (err == EINTR) return read_some(s, dst, want);
    if (nv_net_platform_would_block(err)) {
        s->status = NV_NET_TIMEOUT;
        set_error_text("receive timed out");
        return -1;
    }
    set_error(err);
    s->status = NV_NET_ERROR;
    return -1;
}

const char* nv_net_recv(int handle, int max_bytes) {
    nv_net_slot* s = slot_of(handle);
    if (!s) {
        set_error_text("not an open socket");
        return "";
    }
    if (max_bytes <= 0 || max_bytes > (1 << 20)) max_bytes = 65536;
    if (!reserve((size_t)max_bytes + 1)) return "";
    int n = read_some(s, g_buf, max_bytes);
    if (n <= 0) {
        if (n == 0) {
            s->status = NV_NET_CLOSED;
            set_error_text("peer closed the connection");
        }
        g_buf[0] = '\0';
        g_last_len = 0;
        return g_buf;
    }
    g_buf[n] = '\0';
    s->status = NV_NET_OK;
    s->last_len = n;
    g_last_len = n;
    return g_buf;
}

const char* nv_net_recv_until(int handle, const char* delim, int max_bytes) {
    nv_net_slot* s = slot_of(handle);
    if (!s || !delim || !delim[0]) {
        set_error_text("recv_until needs a delimiter");
        return "";
    }
    if (max_bytes <= 0 || max_bytes > (1 << 20)) max_bytes = 65536;
    const size_t dlen = strlen(delim);
    if (!reserve((size_t)max_bytes + NV_NET_PEND_MAX + 1)) return "";
    size_t used = 0;
    for (;;) {
        g_buf[used] = '\0';
        char* hit = NULL;
        for (size_t i = 0; i + dlen <= used; ++i) {
            if (g_buf[i] == delim[0] && memcmp(g_buf + i, delim, dlen) == 0) {
                hit = g_buf + i;
                break;
            }
        }
        if (hit) {
            // Everything past the delimiter is kept for the next read.
            const size_t extra = used - (size_t)(hit - g_buf) - dlen;
            if (extra > 0 && extra <= NV_NET_PEND_MAX) {
                memcpy(s->pend, hit + dlen, extra);
                s->pend_len = (int)extra;
            }
            *hit = '\0';
            s->status = NV_NET_OK;
            g_last_len = (int)(hit - g_buf);
            return g_buf;
        }
        if ((int)used >= max_bytes) {
            g_buf[used] = '\0';
            s->status = NV_NET_OK;
            g_last_len = (int)used;
            return g_buf;
        }
        if (!reserve((size_t)max_bytes + NV_NET_PEND_MAX + 1)) return "";
        int n = read_some(s, g_buf + used, max_bytes - (int)used);
        if (n <= 0) {
            if (n == 0) {
                s->status = NV_NET_CLOSED;
                set_error_text("peer closed the connection");
            }
            // What arrived before the close is still returned (a stream without a final
            // delimiter is a normal case, not an error).
            const int partial = used > 0;
            if (!partial && s->status != NV_NET_CLOSED) g_buf[0] = '\0';
            g_buf[used] = '\0';
            g_last_len = (int)used;
            return g_buf;
        }
        used += (size_t)n;
    }
}

const char* nv_net_recv_exact(int handle, int nbytes) {
    nv_net_slot* s = slot_of(handle);
    if (!s || nbytes <= 0 || nbytes > (1 << 20)) {
        set_error_text("recv_exact needs a positive length");
        return "";
    }
    if (!reserve((size_t)nbytes + 1)) return "";
    int used = 0;
    while (used < nbytes) {
        int n = read_some(s, g_buf + used, nbytes - used);
        if (n <= 0) {
            if (n == 0) {
                s->status = NV_NET_CLOSED;
                set_error_text("peer closed the connection");
            }
            g_buf[0] = '\0';
            g_last_len = 0;
            return g_buf;
        }
        used += n;
    }
    g_buf[used] = '\0';
    s->status = NV_NET_OK;
    s->last_len = used;
    g_last_len = used;
    return g_buf;
}

int nv_net_shutdown(int handle, int how) {
    nv_net_slot* s = slot_of(handle);
    if (!s) return -1;
#ifdef _WIN32
    int mode = SD_BOTH;
    if (how == 0) mode = SD_SEND;
    else if (how == 1) mode = SD_RECEIVE;
#else
    int mode = SHUT_RDWR;
    if (how == 0) mode = SHUT_WR;
    else if (how == 1) mode = SHUT_RD;
#endif
    return shutdown(s->fd, mode) == 0 ? 0 : -1;
}

int nv_net_close(int handle) {
    nv_net_slot* s = slot_of(handle);
    if (!s) return -1;
    if (s->fd != NV_INVALID_SOCK) nv_sock_close(s->fd);
    s->fd = NV_INVALID_SOCK;
    s->kind = NV_NET_FREE;
    s->pend_len = 0;
    return 0;
}

// ── options and timing ──────────────────────────────────────────────────────

int nv_net_set_timeout(int handle, int ms) {
    nv_net_slot* s = slot_of(handle);
    if (!s) return -1;
    if (ms < 0) ms = 0;
    nv_net_set_sock_timeout(s->fd, SO_RCVTIMEO, ms);
    nv_net_set_sock_timeout(s->fd, SO_SNDTIMEO, ms);
    return 0;
}

int nv_net_set_option(int handle, const char* name, int value) {
    nv_net_slot* s = slot_of(handle);
    if (!s || !name) return -1;
    int level = SOL_SOCKET;
    int opt = 0;
    if (strcmp(name, "nodelay") == 0) { level = IPPROTO_TCP; opt = TCP_NODELAY; }
    else if (strcmp(name, "reuseaddr") == 0) opt = SO_REUSEADDR;
    else if (strcmp(name, "keepalive") == 0) opt = SO_KEEPALIVE;
    else if (strcmp(name, "broadcast") == 0) opt = SO_BROADCAST;
    else if (strcmp(name, "rcvbuf") == 0) opt = SO_RCVBUF;
    else if (strcmp(name, "sndbuf") == 0) opt = SO_SNDBUF;
    else if (strcmp(name, "linger_ms") == 0) {
        struct linger lg;
        lg.l_onoff = value > 0 ? 1 : 0;
        lg.l_linger = value;
        return setsockopt(s->fd, SOL_SOCKET, SO_LINGER, (const char*)&lg, sizeof(lg)) == 0 ? 0 : -1;
    } else if (strcmp(name, "nonblocking") == 0) {
        s->nonblocking = value ? 1 : 0;
        return nv_net_set_nonblocking(s->fd, s->nonblocking);
    } else {
        set_error_text("unknown socket option");
        return -1;
    }
    return setsockopt(s->fd, level, opt, (const char*)&value, sizeof(value)) == 0 ? 0 : -1;
}

int nv_net_get_option(int handle, const char* name) {
    nv_net_slot* s = slot_of(handle);
    if (!s || !name) return -1;
    int level = SOL_SOCKET;
    int opt = 0;
    if (strcmp(name, "nodelay") == 0) { level = IPPROTO_TCP; opt = TCP_NODELAY; }
    else if (strcmp(name, "reuseaddr") == 0) opt = SO_REUSEADDR;
    else if (strcmp(name, "keepalive") == 0) opt = SO_KEEPALIVE;
    else if (strcmp(name, "rcvbuf") == 0) opt = SO_RCVBUF;
    else if (strcmp(name, "sndbuf") == 0) opt = SO_SNDBUF;
    else if (strcmp(name, "nonblocking") == 0) return s->nonblocking;
    else if (strcmp(name, "status") == 0) return s->status;
    else return -1;
    int value = 0;
    nv_socklen_t len = (nv_socklen_t)sizeof(value);
    if (getsockopt(s->fd, level, opt, (char*)&value, &len) != 0) return -1;
    return value;
}

int nv_net_status(int handle) {
    nv_net_slot* s = slot_of(handle);
    return s ? s->status : NV_NET_ERROR;
}

const char* nv_net_last_error(void) { return g_error; }

// ── addresses of a live socket ──────────────────────────────────────────────

static const char* endpoint_of(int handle, int peer) {
    nv_net_slot* s = slot_of(handle);
    if (!s) return "";
    if (!peer && s->kind == NV_NET_UDP && s->peer[0]) return s->peer;   // last datagram sender
    struct sockaddr_storage addr;
    nv_socklen_t len = (nv_socklen_t)sizeof(addr);
    int rc = peer ? getpeername(s->fd, (struct sockaddr*)&addr, &len)
                  : getsockname(s->fd, (struct sockaddr*)&addr, &len);
    if (rc != 0) return "";
    if (!reserve(NV_NET_ADDR_MAX)) return "";
    format_addr((struct sockaddr*)&addr, len, g_buf, g_buf_cap);
    return g_buf;
}

const char* nv_net_peer_addr(int handle) { return endpoint_of(handle, 1); }
const char* nv_net_local_addr(int handle) { return endpoint_of(handle, 0); }

int nv_net_local_port(int handle) {
    const char* text = endpoint_of(handle, 0);
    const char* colon = text ? strrchr(text, ':') : NULL;
    return colon ? atoi(colon + 1) : -1;
}

// ── poll ────────────────────────────────────────────────────────────────────

const char* nv_net_poll(const char* handles_csv, const char* mode, int timeout_ms) {
    if (!reserve(256)) return "";
    nv_pollfd_t fds[NV_NET_MAX];
    int handles[NV_NET_MAX];
    int count = 0;
    const int want_read = !mode || strchr(mode, 'r') != NULL;
    const int want_write = mode && strchr(mode, 'w') != NULL;
    const char* p = handles_csv ? handles_csv : "";
    while (*p && count < NV_NET_MAX) {
        char* end = NULL;
        long h = strtol(p, &end, 10);
        if (end == p) break;
        nv_net_slot* s = slot_of((int)h);
        if (s) {
            handles[count] = (int)h;
            fds[count].fd = s->fd;
            fds[count].events = (short)((want_read ? POLLIN : 0) | (want_write ? POLLOUT : 0));
            fds[count].revents = 0;
            ++count;
        }
        p = end;
        while (*p == ',' || *p == ' ') ++p;
    }
    if (count == 0) return "";
    int rc = nv_poll(fds, count, timeout_ms);
    if (rc < 0) {
        set_error(nv_net_platform_errno());
        return "";
    }
    g_buf[0] = '\0';
    for (int i = 0; i < count; ++i) {
        if (!fds[i].revents) continue;
        if (g_buf[0]) strncat(g_buf, ",", g_buf_cap - strlen(g_buf) - 1);
        char num[16];
        snprintf(num, sizeof(num), "%d", handles[i]);
        strncat(g_buf, num, g_buf_cap - strlen(g_buf) - 1);
    }
    return g_buf;
}

// ── hex in / hex out ────────────────────────────────────────────────────────
//
// A Narval string is NUL-terminated, so a protocol that carries binary (bTLS records, the
// IPv69 wire) travels as hex text until it reaches the socket, where it becomes bytes
// again. These two are what makes Frame able to carry a payload with NULs in it.

static char* g_hex_buf = NULL;
static size_t g_hex_cap = 0;

static char* hex_buf(size_t need) {
    if (need > g_hex_cap) {
        size_t want = need < 512 ? 512 : need;
        char* p = (char*)realloc(g_hex_buf, want);
        if (!p) return NULL;
        g_hex_buf = p;
        g_hex_cap = want;
    }
    return g_hex_buf;
}

static char hx_char(int v) { return (char)(v < 10 ? '0' + v : 'a' + (v - 10)); }

static int hx_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int nv_net_send_hex(int handle, const char* hex) {
    nv_net_slot* s = slot_of(handle);
    if (!s) return -1;
    if (!hex || !nv_net_hex_valid(hex)) {
        s->status = NV_NET_ERROR;
        set_error_text("not a hex string");
        return -1;
    }
    size_t n = strlen(hex) / 2;
    if (!reserve(n + 1)) return -1;
    for (size_t i = 0; i < n; i++) {
        int hi = hx_digit(hex[i * 2]), lo = hx_digit(hex[i * 2 + 1]);
        g_buf[i] = (char)(((hi << 4) | lo) & 0xFF);
    }
    g_buf[n] = '\0';
    return nv_net_send_bytes(handle, g_buf, (int)n);
}

const char* nv_net_recv_exact_hex(int handle, int nbytes) {
    if (!slot_of(handle) || nbytes < 0) return "";
    const char* raw = nv_net_recv_exact(handle, nbytes);
    if (!raw) return "";
    int got = nv_net_last_len();
    if (got < 0) got = 0;
    if (got > nbytes) got = nbytes;
    char* out = hex_buf((size_t)got * 2 + 1);
    if (!out) return "";
    for (int i = 0; i < got; i++) {
        unsigned char c = (unsigned char)raw[i];
        out[i * 2] = hx_char((c >> 4) & 0x0F);
        out[i * 2 + 1] = hx_char(c & 0x0F);
    }
    out[(size_t)got * 2] = '\0';
    return out;
}

// ── Narval-facing wrappers ──────────────────────────────────────────────────

static const char* arg_str(NvObject* o) {
    return (o && o->ob_type == NVStr_Type) ? ((NVStr*)o)->value : NULL;
}

static int arg_int(NvObject* o, int fallback) {
    return (o && o->ob_type == NVInt_Type) ? (int)((NVInt*)o)->value : fallback;
}

static NvObject* box_int(long v) {
    Value out = {NULL};
    create_int(&out, (int32_t)v);
    return out.obj;
}

static NvObject* box_str(const char* s) {
    Value out = {NULL};
    create_str(&out, s ? s : "");
    return out.obj;
}

NvObject* nv_net_tcp_listen_builtin(NvObject* host, NvObject* port) {
    return box_int(nv_net_tcp_listen(arg_str(host), arg_int(port, 0)));
}

NvObject* nv_net_tcp_connect_builtin(NvObject* host, NvObject* port, NvObject* timeout_ms) {
    return box_int(nv_net_tcp_connect(arg_str(host), arg_int(port, 0), arg_int(timeout_ms, 0)));
}

NvObject* nv_net_accept_builtin(NvObject* listener, NvObject* timeout_ms) {
    return box_int(nv_net_accept(arg_int(listener, -1), arg_int(timeout_ms, 0)));
}

NvObject* nv_net_udp_bind_builtin(NvObject* host, NvObject* port) {
    return box_int(nv_net_udp_bind(arg_str(host), arg_int(port, 0)));
}

NvObject* nv_net_udp_send_to_builtin(NvObject* h, NvObject* host, NvObject* port, NvObject* data) {
    return box_int(nv_net_udp_send_to(arg_int(h, -1), arg_str(host), arg_int(port, 0), arg_str(data)));
}

NvObject* nv_net_udp_recv_from_builtin(NvObject* h, NvObject* max_bytes) {
    return box_str(nv_net_udp_recv_from(arg_int(h, -1), arg_int(max_bytes, 65535)));
}

NvObject* nv_net_send_builtin(NvObject* h, NvObject* data) {
    return box_int(nv_net_send(arg_int(h, -1), arg_str(data)));
}

NvObject* nv_net_recv_builtin(NvObject* h, NvObject* max_bytes) {
    return box_str(nv_net_recv(arg_int(h, -1), arg_int(max_bytes, 65536)));
}

NvObject* nv_net_recv_until_builtin(NvObject* h, NvObject* delim, NvObject* max_bytes) {
    return box_str(nv_net_recv_until(arg_int(h, -1), arg_str(delim), arg_int(max_bytes, 65536)));
}

NvObject* nv_net_recv_exact_builtin(NvObject* h, NvObject* nbytes) {
    return box_str(nv_net_recv_exact(arg_int(h, -1), arg_int(nbytes, 0)));
}

NvObject* nv_net_shutdown_builtin(NvObject* h, NvObject* how) {
    return box_int(nv_net_shutdown(arg_int(h, -1), arg_int(how, 2)));
}

NvObject* nv_net_close_builtin(NvObject* h) { return box_int(nv_net_close(arg_int(h, -1))); }

NvObject* nv_net_set_timeout_builtin(NvObject* h, NvObject* ms) {
    return box_int(nv_net_set_timeout(arg_int(h, -1), arg_int(ms, 0)));
}

NvObject* nv_net_set_option_builtin(NvObject* h, NvObject* name, NvObject* value) {
    return box_int(nv_net_set_option(arg_int(h, -1), arg_str(name), arg_int(value, 0)));
}

NvObject* nv_net_get_option_builtin(NvObject* h, NvObject* name) {
    return box_int(nv_net_get_option(arg_int(h, -1), arg_str(name)));
}

NvObject* nv_net_status_builtin(NvObject* h) { return box_int(nv_net_status(arg_int(h, -1))); }

NvObject* nv_net_last_error_builtin(void) { return box_str(nv_net_last_error()); }

NvObject* nv_net_last_len_builtin(void) { return box_int(nv_net_last_len()); }

NvObject* nv_net_local_port_builtin(NvObject* h) { return box_int(nv_net_local_port(arg_int(h, -1))); }

NvObject* nv_net_local_addr_builtin(NvObject* h) { return box_str(nv_net_local_addr(arg_int(h, -1))); }

NvObject* nv_net_peer_addr_builtin(NvObject* h) { return box_str(nv_net_peer_addr(arg_int(h, -1))); }

NvObject* nv_net_poll_builtin(NvObject* handles, NvObject* mode, NvObject* timeout_ms) {
    return box_str(nv_net_poll(arg_str(handles), arg_str(mode), arg_int(timeout_ms, 0)));
}

NvObject* nv_net_send_hex_builtin(NvObject* handle, NvObject* hex) {
    return box_int(nv_net_send_hex(arg_int(handle, -1), arg_str(hex)));
}

NvObject* nv_net_recv_exact_hex_builtin(NvObject* handle, NvObject* nbytes) {
    return box_str(nv_net_recv_exact_hex(arg_int(handle, -1), arg_int(nbytes, -1)));
}
