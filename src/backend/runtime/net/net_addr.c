// net_addr.c — addresses and names for the `net` stdlib.
//
// One textual shape travels through the language: "host:port", with IPv6 kept in brackets
// ("[::1]:8080"). Everything else (bare v6, ":port", a trailing slash) is normalised here, so
// a class like Address never has to guess.
//
// Resolution uses getaddrinfo: the numeric fast path first (no DNS round trip for "127.0.0.1"),
// then the resolver. On a *statically* linked POSIX program glibc cannot load nss_dns, so only
// /etc/hosts answers — that is a property of static linking, not of this code (the IPv69
// project hit the same wall and wrote its own resolver for it).

#include "net_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend/runtime/net_bridge.h"

#define NV_ADDR_HOST_MAX 128
#define NV_ADDR_TEXT_MAX 192

static char g_addr_buf[8192];
static char g_error[256] = "";
static int g_ready = 0;

static char* addr_buffer(size_t need) {
    if (need > sizeof(g_addr_buf)) return NULL;
    return g_addr_buf;
}

// "host:port" -> host/port (the inverse of the canonical form below).
static int split_host_port(const char* text, char* host, size_t host_len, int* port) {
    if (!text || !text[0]) return -1;
    host[0] = '\0';
    *port = 0;
    const char* colon = NULL;
    if (text[0] == '[') {                       // [v6]:port  /  [v6]
        const char* close = strchr(text, ']');
        if (!close) return -1;
        const size_t n = (size_t)(close - text - 1);
        if (n >= host_len) return -1;
        memcpy(host, text + 1, n);
        host[n] = '\0';
        if (close[1] == ':') *port = atoi(close + 2);
        else if (close[1] != '\0') return -1;
        return 0;
    }
    const char* first = strchr(text, ':');
    const char* last = strrchr(text, ':');
    if (first && first == last) colon = first;   // exactly one colon: host:port
    else if (last && first != last) colon = last; // a bare IPv6 literal
    if (!colon) {                                // no colon at all: a bare host
        if (strlen(text) >= host_len) return -1;
        snprintf(host, host_len, "%s", text);
        return 0;
    }
    if (first == last) {                         // host:port
        const size_t n = (size_t)(colon - text);
        if (n >= host_len) return -1;
        memcpy(host, text, n);
        host[n] = '\0';
        *port = atoi(colon + 1);
        return 0;
    }
    // Bare IPv6 without brackets: "fe80::1" — the whole text is the host, no port.
    if (strlen(text) >= host_len) return -1;
    snprintf(host, host_len, "%s", text);
    return 0;
}

static int validate_port(int port) { return port >= 0 && port <= 65535; }

const char* nv_net_addr_parse(const char* text) {
    char host[NV_ADDR_HOST_MAX];
    int port = 0;
    if (split_host_port(text, host, sizeof(host), &port) != 0 || !validate_port(port)) return "";
    if (!addr_buffer(NV_ADDR_TEXT_MAX)) return "";
    if (host[0] == '\0') snprintf(g_addr_buf, sizeof(g_addr_buf), ":%d", port);
    else if (strchr(host, ':')) snprintf(g_addr_buf, sizeof(g_addr_buf), "[%s]:%d", host, port);
    else snprintf(g_addr_buf, sizeof(g_addr_buf), "%s:%d", host, port);
    return g_addr_buf;
}

// Numeric first: a literal address must not cost a DNS query.
static int lookup(const char* host, int numeric_only, struct addrinfo** out) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (numeric_only) hints.ai_flags = AI_NUMERICHOST;
    int rc = getaddrinfo(host, NULL, &hints, out);
    return rc == 0 && *out ? 0 : -1;
}

const char* nv_net_resolve(const char* host) {
    nv_net_init();
    if (!host || !host[0]) return "";
    if (!addr_buffer(NV_ADDR_TEXT_MAX)) return "";
    struct addrinfo* res = NULL;
    if (lookup(host, 0, &res) != 0) {
        snprintf(g_error, sizeof(g_error), "cannot resolve '%s'", host);
        return "";
    }
    char numeric[128] = "";
    if (getnameinfo(res->ai_addr, (nv_socklen_t)res->ai_addrlen, numeric, (nv_socklen_t)sizeof(numeric),
                    NULL, 0, NI_NUMERICHOST) != 0)
        numeric[0] = '\0';
    snprintf(g_addr_buf, sizeof(g_addr_buf), "%s", numeric);
    freeaddrinfo(res);
    return g_addr_buf;
}

const char* nv_net_resolve_all(const char* host) {
    nv_net_init();
    if (!host || !host[0] || !addr_buffer(sizeof(g_addr_buf))) return "";
    struct addrinfo* res = NULL;
    if (lookup(host, 0, &res) != 0) {
        snprintf(g_error, sizeof(g_error), "cannot resolve '%s'", host);
        return "";
    }
    g_addr_buf[0] = '\0';
    for (struct addrinfo* it = res; it; it = it->ai_next) {
        char numeric[128] = "";
        if (getnameinfo(it->ai_addr, (nv_socklen_t)it->ai_addrlen, numeric,
                        (nv_socklen_t)sizeof(numeric), NULL, 0, NI_NUMERICHOST) != 0)
            continue;
        if (g_addr_buf[0]) strncat(g_addr_buf, ";", sizeof(g_addr_buf) - strlen(g_addr_buf) - 1);
        strncat(g_addr_buf, numeric, sizeof(g_addr_buf) - strlen(g_addr_buf) - 1);
    }
    freeaddrinfo(res);
    return g_addr_buf;
}

const char* nv_net_hostname(void) {
    nv_net_init();
    static char name[256];
    name[0] = '\0';
    if (gethostname(name, (int)sizeof(name) - 1) != 0) {
        snprintf(g_error, sizeof(g_error), "cannot read the host name");
        return "";
    }
    name[sizeof(name) - 1] = '\0';
    return name;
}

// Host and port of "host:port" (brackets of an IPv6 literal are stripped). Split here, in
// C, not in Narval: this is the shape every protocol needs, and it must be exact.
const char* nv_net_host_of(const char* addr) {
    static char host[NV_ADDR_HOST_MAX];
    int port = 0;
    if (split_host_port(addr, host, sizeof(host), &port) != 0) return "";
    return host;
}

int nv_net_port_of(const char* addr) {
    char host[NV_ADDR_HOST_MAX];
    int port = 0;
    if (split_host_port(addr, host, sizeof(host), &port) != 0) return 0;
    return port;
}

// ── Narval-facing wrappers ──────────────────────────────────────────────────

static const char* arg_str(NvObject* o) {
    return (o && o->ob_type == NVStr_Type) ? ((NVStr*)o)->value : NULL;
}

static NvObject* box_str(const char* s) {
    Value out = {NULL};
    create_str(&out, s ? s : "");
    return out.obj;
}

NvObject* nv_net_addr_parse_builtin(NvObject* text) {
    return box_str(nv_net_addr_parse(arg_str(text)));
}

NvObject* nv_net_resolve_builtin(NvObject* host) {
    return box_str(nv_net_resolve(arg_str(host)));
}

NvObject* nv_net_resolve_all_builtin(NvObject* host) {
    return box_str(nv_net_resolve_all(arg_str(host)));
}

NvObject* nv_net_hostname_builtin(void) { return box_str(nv_net_hostname()); }

NvObject* nv_net_host_of_builtin(NvObject* addr) {
    return box_str(nv_net_host_of(arg_str(addr)));
}

NvObject* nv_net_port_of_builtin(NvObject* addr) {
    Value out = {NULL};
    create_int(&out, (int32_t)nv_net_port_of(arg_str(addr)));
    return out.obj;
}
