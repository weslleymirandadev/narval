// net_bridge.h — the socket layer Narval's `net` stdlib is written on.
//
// Everything here is C, portable (POSIX + Winsock2) and handle-based: a socket is a small
// integer, exactly like a File handle, because that is what travels through the runtime's
// value model. Nothing in this header knows about Narval values — the `_builtin` wrappers at
// the end of `src/backend/runtime/net/net_socket.c` box and unbox.
//
// Two rules learned the hard way, both encoded in net_platform.h:
//   * timeouts are milliseconds on BOTH platforms (Winsock wants a DWORD in ms, POSIX a
//     struct timeval; passing a timeval there gives a 3 ms timeout and a handshake that
//     never completes);
//   * a failed call never leaves errno/WSAGetLastError to the caller: an empty result plus
//     nv_net_status() says what happened (ok / timed out / peer closed / error).
#ifndef NV_NET_BRIDGE_H
#define NV_NET_BRIDGE_H

#include "backend/runtime/nv_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── lifecycle ────────────────────────────────────────────────────────────────
// Idempotent; starts Winsock on Windows (no-op on POSIX). Every entry point calls it.
void nv_net_init(void);
// 1 when the platform layer came up (Winsock started).
int nv_net_ready(void);

// ── status of the last operation on a handle ─────────────────────────────────
#define NV_NET_OK        0
#define NV_NET_TIMEOUT   1
#define NV_NET_CLOSED    2   // peer closed the connection
#define NV_NET_ERROR    -1
int nv_net_status(int handle);
const char* nv_net_last_error(void);

// ── TCP ─────────────────────────────────────────────────────────────────────
// Bind and listen; host "" (or NULL) = every interface. Handle >= 0, else -1.
int nv_net_tcp_listen(const char* host, int port);
// Connect with a millisecond deadline (0 = blocking, no deadline). Handle >= 0, else -1.
int nv_net_tcp_connect(const char* host, int port, int timeout_ms);
// Accepts one connection: handle >= 0, NV_NET_TIMEOUT_HANDLE when nothing arrived in time.
#define NV_NET_TIMEOUT_HANDLE (-2)
int nv_net_accept(int listener, int timeout_ms);

// ── UDP ─────────────────────────────────────────────────────────────────────
int nv_net_udp_bind(const char* host, int port);
// Sends one datagram; number of bytes sent or -1.
int nv_net_udp_send_to(int handle, const char* host, int port, const char* data);
// Receives one datagram: "host:port|payload" (empty on timeout/error). Split on the first
// '|' — an address never contains one. The payload is raw bytes; NULs inside are preserved
// and the length travels back through nv_net_last_len().
const char* nv_net_udp_recv_from(int handle, int max_bytes);
int nv_net_last_len(void);

// ── streams ─────────────────────────────────────────────────────────────────
// Sends the whole string (loops until written); number of bytes or -1.
int nv_net_send(int handle, const char* data);
// Sends exactly n bytes of a buffer; used by binary protocols.
int nv_net_send_bytes(int handle, const char* data, int len);
// Reads up to max_bytes (blocks until some data, the peer closes, or the timeout fires).
// Empty result: look at nv_net_status().
const char* nv_net_recv(int handle, int max_bytes);
// Reads until `delim` (not included) or max_bytes; the terminator is consumed.
const char* nv_net_recv_until(int handle, const char* delim, int max_bytes);
// Reads exactly n bytes ("" when the peer closes early). For framed protocols.
const char* nv_net_recv_exact(int handle, int nbytes);
// 0 half-closes the send side, 1 the receive side, 2 both.
int nv_net_shutdown(int handle, int how);
int nv_net_close(int handle);

// ── options and timing ──────────────────────────────────────────────────────
// Milliseconds for both directions; 0 = no timeout (block forever).
int nv_net_set_timeout(int handle, int ms);
// name: "nodelay", "reuseaddr", "keepalive", "broadcast", "linger_ms", "rcvbuf", "sndbuf",
// "nonblocking". 0 on success, -1 on an unknown name or a refused option.
int nv_net_set_option(int handle, const char* name, int value);
int nv_net_get_option(int handle, const char* name);

// ── addresses and names ─────────────────────────────────────────────────────
// "host:port" / "[v6]:port" / ":port" -> "host:port" (IPv6 kept bracketed). "" if invalid.
const char* nv_net_addr_parse(const char* text);
// Numeric address of a hostname (first answer); "" when it does not resolve.
const char* nv_net_resolve(const char* host);
// Every answer, ';'-separated, in resolver order.
const char* nv_net_resolve_all(const char* host);
const char* nv_net_hostname(void);
// Host and port of an endpoint; both are exact (the split lives here, not in Narval).
const char* nv_net_host_of(const char* addr);
int nv_net_port_of(const char* addr);
int nv_net_local_port(int handle);
const char* nv_net_local_addr(int handle);
const char* nv_net_peer_addr(int handle);
// CSV of handles; mode "r"/"w"/"rw"; millisecond timeout. Returns the ready handles as CSV.
const char* nv_net_poll(const char* handles_csv, const char* mode, int timeout_ms);

// ── Byte codec (Packet / Frame): hex text in, value out; pure, no handles ──
int nv_net_hex_valid(const char* hex);
int nv_net_hex_len(const char* hex);                 // byte count (0 when malformed)
const char* nv_net_str_to_hex(const char* text);
const char* nv_net_hex_to_str(const char* hex);      // NULs truncate the Narval string
const char* nv_net_pack_int(const char* hex, long long value, int nbytes, int le);
long long nv_net_unpack_int(const char* hex, int pos, int nbytes, int le);
const char* nv_net_unpack_hex(const char* hex, int pos, int nbytes);
const char* nv_net_platform_name(void);              // "linux" / "windows" / "unix"
const char* nv_net_caps(void);                       // csv of what this build can do
int nv_net_send_hex(int handle, const char* hex);            // bytes behind hex -> socket
const char* nv_net_recv_exact_hex(int handle, int nbytes);   // exactly n bytes as hex

// ── Narval-facing wrappers (values in, values out) ──────────────────────────
NvObject* nv_net_tcp_listen_builtin(NvObject* host, NvObject* port);
NvObject* nv_net_tcp_connect_builtin(NvObject* host, NvObject* port, NvObject* timeout_ms);
NvObject* nv_net_accept_builtin(NvObject* listener, NvObject* timeout_ms);
NvObject* nv_net_udp_bind_builtin(NvObject* host, NvObject* port);
NvObject* nv_net_udp_send_to_builtin(NvObject* h, NvObject* host, NvObject* port, NvObject* data);
NvObject* nv_net_udp_recv_from_builtin(NvObject* h, NvObject* max_bytes);
NvObject* nv_net_send_builtin(NvObject* h, NvObject* data);
NvObject* nv_net_recv_builtin(NvObject* h, NvObject* max_bytes);
NvObject* nv_net_recv_until_builtin(NvObject* h, NvObject* delim, NvObject* max_bytes);
NvObject* nv_net_recv_exact_builtin(NvObject* h, NvObject* nbytes);
NvObject* nv_net_shutdown_builtin(NvObject* h, NvObject* how);
NvObject* nv_net_close_builtin(NvObject* h);
NvObject* nv_net_set_timeout_builtin(NvObject* h, NvObject* ms);
NvObject* nv_net_set_option_builtin(NvObject* h, NvObject* name, NvObject* value);
NvObject* nv_net_get_option_builtin(NvObject* h, NvObject* name);
NvObject* nv_net_status_builtin(NvObject* h);
NvObject* nv_net_last_error_builtin(void);
NvObject* nv_net_last_len_builtin(void);
NvObject* nv_net_addr_parse_builtin(NvObject* text);
NvObject* nv_net_resolve_builtin(NvObject* host);
NvObject* nv_net_resolve_all_builtin(NvObject* host);
NvObject* nv_net_hostname_builtin(void);
NvObject* nv_net_host_of_builtin(NvObject* addr);
NvObject* nv_net_port_of_builtin(NvObject* addr);
NvObject* nv_net_local_port_builtin(NvObject* h);
NvObject* nv_net_local_addr_builtin(NvObject* h);
NvObject* nv_net_peer_addr_builtin(NvObject* h);
NvObject* nv_net_poll_builtin(NvObject* handles, NvObject* mode, NvObject* timeout_ms);
NvObject* nv_net_hex_valid_builtin(NvObject* hex);
NvObject* nv_net_hex_len_builtin(NvObject* hex);
NvObject* nv_net_str_to_hex_builtin(NvObject* text);
NvObject* nv_net_hex_to_str_builtin(NvObject* hex);
NvObject* nv_net_pack_int_builtin(NvObject* hex, NvObject* value, NvObject* nbytes, NvObject* le);
NvObject* nv_net_unpack_int_builtin(NvObject* hex, NvObject* pos, NvObject* nbytes, NvObject* le);
NvObject* nv_net_unpack_hex_builtin(NvObject* hex, NvObject* pos, NvObject* nbytes);
NvObject* nv_net_platform_builtin(void);
NvObject* nv_net_caps_builtin(void);
NvObject* nv_net_send_hex_builtin(NvObject* handle, NvObject* hex);
NvObject* nv_net_recv_exact_hex_builtin(NvObject* handle, NvObject* nbytes);

#ifdef __cplusplus
}
#endif

#endif  // NV_NET_BRIDGE_H
