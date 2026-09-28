// l2_bridge.c — o L2 cru do IPv69: AF_PACKET no EtherType 0x6969.
//
// O espelho de src/IPv69/l2.c (raw_socket/send_frame/recv), reduzido ao que o Narval precisa:
// o frame INTEIRO entra e sai (eth header incluído), porque quem o monta é o bait.nv em Narval.
// Só filtra o que interessa: o socket já nasce preso ao EtherType 0x6969.
#include <string.h>
#include <stdio.h>

#include "backend/runtime/net_bridge.h"
#include "backend/runtime/modules/l2_decls.h"

#ifndef _WIN32
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>

#define L2_MAX_FRAME 1600
#define ETH_P_IPV69 0x6969

typedef struct {
    int fd;
    int ifindex;
    unsigned char mac[6];
} l2_handle_t;

static l2_handle_t g_l2[32];

/* devolve o handle (índice) ou -1; o -1 do socket também é -1 aqui */
int nv_l2_open(const char* ifname)
{
    int slot = -1;
    for (int i = 0; i < 32; i++) if (g_l2[i].fd == 0) { slot = i; break; }
    if (slot < 0) return -1;

    int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd < 0) return -1;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, ifname ? ifname : "", IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) { close(fd); return -1; }
    /* O índice tem de ser capturado AQUI: o SIOCGIFHWADDR reusa a mesma struct (com memset
       antes) e apaga o ifr_ifindex. Guardar depois dava sendto com índice inválido — ENXIO,
       "No such device or address". */
    int ifindex = ifr.ifr_ifindex;

    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof sll);
    sll.sll_family = AF_PACKET;
    sll.sll_protocol = htons(ETH_P_ALL);
    sll.sll_ifindex = ifindex;
    if (bind(fd, (struct sockaddr*)&sll, sizeof sll) < 0) { close(fd); return -1; }

    memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, ifname ? ifname : "", IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) { close(fd); return -1; }

    g_l2[slot].fd = fd;
    g_l2[slot].ifindex = ifindex;
    memcpy(g_l2[slot].mac, ifr.ifr_hwaddr.sa_data, 6);
    return slot;
}

static const char* mac_hex(const unsigned char* m)
{
    static char out[13];
    static const char* d = "0123456789abcdef";
    for (int i = 0; i < 6; i++) { out[i * 2] = d[m[i] >> 4]; out[i * 2 + 1] = d[m[i] & 15]; }
    out[12] = 0;
    return out;
}

const char* nv_l2_mac(int h)
{
    if (h < 0 || h >= 32 || !g_l2[h].fd) return "";
    return mac_hex(g_l2[h].mac);
}

static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int nv_l2_send(int h, const char* frame_hex)
{
    if (h < 0 || h >= 32 || !g_l2[h].fd || !frame_hex) return -1;
    unsigned char frame[L2_MAX_FRAME];
    size_t n = strlen(frame_hex) / 2;
    if (n < 14 || n > sizeof frame) return -1;
    for (size_t i = 0; i < n; i++) {
        int hi = hexv(frame_hex[i * 2]), lo = hexv(frame_hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return -1;
        frame[i] = (unsigned char)(hi * 16 + lo);
    }
    struct sockaddr_ll dst;
    memset(&dst, 0, sizeof dst);
    dst.sll_family = AF_PACKET;
    /* O protocolo de destino é obrigatório aqui: com zero o sendto falha em silêncio. É o
       mesmo que o send_frame dele preenche (htons(ETHERTYPE_IPV69)). */
    dst.sll_protocol = htons(ETH_P_IPV69);
    dst.sll_ifindex = g_l2[h].ifindex;
    dst.sll_halen = 6;
    memcpy(dst.sll_addr, frame, 6);            /* o eth header já traz o destino */
    if (sendto(g_l2[h].fd, frame, n, 0, (struct sockaddr*)&dst, sizeof dst) < 0) {
        perror("l2 sendto");
        return -1;
    }
    return 0;
}

const char* nv_l2_recv(int h, int timeout_ms)
{
    static char out[L2_MAX_FRAME * 2 + 1];
    if (h < 0 || h >= 32 || !g_l2[h].fd) return "";
    /* timeout_ms == 0 significa "so o que ja chegou": o laco do chat chama sem parar para
       imprimir na hora, e nao pode ficar preso esperando o proximo frame. */
    int flags = MSG_DONTWAIT;
    if (timeout_ms > 0) {
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(g_l2[h].fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        flags = 0;
    }

    unsigned char frame[L2_MAX_FRAME];
    ssize_t n = recv(g_l2[h].fd, frame, sizeof frame, flags);
    if (n <= 0) return "";
    /* só o que é IPv69: o socket é ETH_P_ALL, então o filtro é aqui — como o bind do l2.c */
    if (n < 14 || frame[12] != 0x69 || frame[13] != 0x69) return "";
    static const char* d = "0123456789abcdef";
    for (ssize_t i = 0; i < n; i++) {
        out[i * 2] = d[frame[i] >> 4];
        out[i * 2 + 1] = d[frame[i] & 15];
    }
    out[n * 2] = 0;
    return out;
}

int nv_l2_close(int h)
{
    if (h < 0 || h >= 32 || !g_l2[h].fd) return -1;
    close(g_l2[h].fd);
    g_l2[h].fd = 0;
    return 0;
}
#else
/* Windows: o caminho é Npcap (o l2_win.c dele). Sem wpcap linkado aqui, o módulo existe e
   devolve erro — o PoC diz o porquê em vez de quebrar o build. */
int nv_l2_open(const char* ifname) { (void)ifname; return -1; }
const char* nv_l2_mac(int h) { (void)h; return ""; }
int nv_l2_send(int h, const char* frame_hex) { (void)h; (void)frame_hex; return -1; }
const char* nv_l2_recv(int h, int timeout_ms) { (void)h; (void)timeout_ms; return ""; }
int nv_l2_close(int h) { (void)h; return -1; }
#endif

static long long arg_int(NvObject* o, long long fallback)
{
    if (!o) return fallback;
    if (o->ob_type == NVInt_Type) return (long long)((NVInt*)o)->value;
    return fallback;
}

static NvObject* box_str(const char* s) { Value out = {NULL}; create_str(&out, s ? s : ""); return out.obj; }
static NvObject* box_int(long long v) { Value out = {NULL}; create_int(&out, (int64_t)v); return out.obj; }

NvObject* nv_l2_open_builtin(NvObject* ifname)
{
    const char* n = (ifname && ifname->ob_type == NVStr_Type) ? ((NVStr*)ifname)->value : "";
    return box_int(nv_l2_open(n));
}

NvObject* nv_l2_mac_builtin(NvObject* h) { return box_str(nv_l2_mac((int)arg_int(h, -1))); }
NvObject* nv_l2_send_builtin(NvObject* h, NvObject* frame)
{
    const char* f = (frame && frame->ob_type == NVStr_Type) ? ((NVStr*)frame)->value : "";
    return box_int(nv_l2_send((int)arg_int(h, -1), f));
}
NvObject* nv_l2_recv_builtin(NvObject* h, NvObject* t)
{
    return box_str(nv_l2_recv((int)arg_int(h, -1), (int)arg_int(t, 0)));
}
NvObject* nv_l2_close_builtin(NvObject* h) { return box_int(nv_l2_close((int)arg_int(h, -1))); }
