/* ed25519.c - standalone Ed25519 wrapper over TweetNaCl.
 *
 * The TweetNaCl crypto_sign API is awkward to use directly: it writes
 * [sig 64][msg] concatenated and crypto_sign_open mutates its input
 * buffer. This wrapper exposes the natural sign/verify split (sig and
 * msg kept separate), so callers never touch TweetNaCl internals.
 */
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ed25519.h"
#include "tweetnacl.h"

int ed25519_keypair(uint8_t sk[ED25519_SK_LEN], uint8_t pub[ED25519_PUB_LEN])
{
    return crypto_sign_keypair(pub, sk);
}

int ed25519_seed_to_pub(uint8_t pub[ED25519_PUB_LEN],
                        const uint8_t seed[ED25519_SEED_LEN])
{
    return crypto_sign_seed_to_pk(pub, seed);
}

int ed25519_sign(uint8_t sig[ED25519_SIG_LEN], const uint8_t *msg, size_t n,
                 const uint8_t sk[ED25519_SK_LEN])
{
    uint8_t full_sk[ED25519_SK_LEN];
    uint8_t *sm = malloc(ED25519_SIG_LEN + n);
    unsigned long long smlen;

    if (!sm)
        return -1;
    /* TweetNaCl needs sk[64] = seed || pub (pub enters the hash); derive
       it here so callers only need to keep the 32-byte seed */
    memcpy(full_sk, sk, ED25519_SEED_LEN);
    ed25519_seed_to_pub(full_sk + ED25519_SEED_LEN, sk);
    crypto_sign(sm, &smlen, msg, n, full_sk);
    memcpy(sig, sm, ED25519_SIG_LEN);
    free(sm);
    return 0;
}

int ed25519_verify(const uint8_t *msg, size_t n,
                   const uint8_t sig[ED25519_SIG_LEN],
                   const uint8_t pub[ED25519_PUB_LEN])
{
    uint8_t *sm = malloc(ED25519_SIG_LEN + n);
    uint8_t *m = malloc(ED25519_SIG_LEN + n);
    unsigned long long mlen;
    int rc;

    if (!sm || !m) {
        free(sm);
        free(m);
        return -1;
    }
    memcpy(sm, sig, ED25519_SIG_LEN);
    memcpy(sm + ED25519_SIG_LEN, msg, n);
    rc = crypto_sign_open(m, &mlen, sm, ED25519_SIG_LEN + n, pub);
    free(sm);
    free(m);
    return rc == 0 ? 0 : -1;
}

int ed25519_keyfile_load_or_create(const char *path,
                                   uint8_t sk[ED25519_SK_LEN],
                                   uint8_t pub[ED25519_PUB_LEN])
{
    char seedhex[65], line[80];
    FILE *f;

    f = fopen(path, "r");
    if (f) {
        if (!fgets(line, sizeof(line), f)) {
            fclose(f);
            return -1;
        }
        fclose(f);
        line[strcspn(line, "\n")] = 0;
        if (strlen(line) != 64)
            return -1;
        for (int i = 0; i < ED25519_SEED_LEN; i++) {
            unsigned v;
            if (sscanf(line + 2 * i, "%2x", &v) != 1)
                return -1;
            sk[i] = (uint8_t)v;
        }
        ed25519_seed_to_pub(sk + 32, sk);
        memcpy(pub, sk + 32, ED25519_PUB_LEN);
        return 0;
    }

    /* first run: generate and persist */
    if (ed25519_keypair(sk, pub) != 0)
        return -1;
    for (int i = 0; i < ED25519_SEED_LEN; i++)
        snprintf(seedhex + 2 * i, 3, "%02x", sk[i]);
    seedhex[64] = 0;
    char dir[256];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = 0;
#ifndef _WIN32
        mkdir(dir, 0700);
#else
        _mkdir(dir);
#endif
    }
    f = fopen(path, "w");
    if (!f)
        return -1;
#ifndef _WIN32
    fchmod(fileno(f), 0600);
#endif
    fprintf(f, "%s\n", seedhex);
    fclose(f);
    printf("ipv69: key generated at %s\n", path);
    printf("ipv69: register this PUBKEY on the server (--peer or --peer-file):\n");
    for (int i = 0; i < ED25519_PUB_LEN; i++)
        printf("%02x", pub[i]);
    printf("\n");
    return 0;
}

void ed25519_keyfile_default_path(char *out, size_t outsz)
{
    const char *home = getenv("HOME");

    if (!home || !*home)
        home = "/root";
    snprintf(out, outsz, "%s/.ipv69/key", home);
}

void ed25519_sha512(uint8_t out[64], const uint8_t *msg, size_t n)
{
    crypto_hash(out, msg, n);
}

void ed25519_pub_to_x25519(uint8_t out[32], const uint8_t ed_pub[32])
{
    extern void crypto_ed25519_to_x25519(uint8_t *out, const uint8_t *ed);
    crypto_ed25519_to_x25519(out, ed_pub);
}

void ed25519_hmac_sha512(uint8_t out[64], const uint8_t *msg, size_t n,
                         const uint8_t *key, size_t klen)
{
    uint8_t k[128];
    uint8_t ipad[128], opad[128];
    uint8_t inner[64];

    memset(k, 0, sizeof(k));
    if (klen > sizeof(k)) {
        ed25519_sha512(k, key, klen);
    } else {
        memcpy(k, key, klen);
    }
    for (size_t i = 0; i < sizeof(k); i++) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }
    /* inner = SHA512(ipad || msg) */
    {
        uint8_t buf[128 + 2048];
        memcpy(buf, ipad, sizeof(ipad));
        memcpy(buf + sizeof(ipad), msg, n);
        ed25519_sha512(inner, buf, sizeof(ipad) + n);
    }
    {
        uint8_t buf[128 + 64];
        memcpy(buf, opad, sizeof(opad));
        memcpy(buf + sizeof(opad), inner, 64);
        ed25519_sha512(out, buf, sizeof(opad) + 64);
    }
}

void ed25519_poly1305(uint8_t out[16], const uint8_t *msg, size_t n,
                      const uint8_t key[32])
{
    crypto_onetimeauth(out, msg, (unsigned long long)n, key);
}

int ed25519_poly1305_verify(const uint8_t tag[16], const uint8_t *msg,
                            size_t n, const uint8_t key[32])
{
    return crypto_onetimeauth_verify(tag, msg, (unsigned long long)n, key);
}

void ed25519_secretbox(uint8_t *c, const uint8_t *m, size_t n,
                       const uint8_t nonce[24], const uint8_t key[32])
{
    /* TweetNaCl secretbox pads 32 zeros in front of the message:
       d = n + 32, plaintext lives at m[32..]. */
    uint8_t buf[32 + 2048];
    if (n > sizeof(buf) - 32) {
        /* callers use small keys; refuse oversized input */
        memset(c, 0, n + 32);
        return;
    }
    memset(buf, 0, 32);
    memcpy(buf + 32, m, n);
    crypto_secretbox(c, buf, n + 32, nonce, key);
}

int ed25519_secretbox_open(uint8_t *m, const uint8_t *c, size_t n,
                           const uint8_t nonce[24], const uint8_t key[32])
{
    uint8_t buf[1024 + 32];
    uint8_t *b = buf;

    if (n > sizeof(buf) - 32)
        return -1;
    if (crypto_secretbox_open(b, c, n + 32, nonce, key) != 0)
        return -1;
    memcpy(m, b + 32, n);
    return 0;
}

int ed25519_scalarmult(uint8_t q[32], const uint8_t n[32], const uint8_t p[32])
{
    return crypto_scalarmult(q, n, p);
}

int ed25519_scalarmult_base(uint8_t q[32], const uint8_t n[32])
{
    return crypto_scalarmult_base(q, n);
}
