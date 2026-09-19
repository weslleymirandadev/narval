/* ed25519.h - standalone Ed25519 (RFC 8032) + persistent key store.
 *
 * Thin, dependency-free wrapper over TweetNaCl (public domain). The IPv69
 * stack uses it to sign DHCP69 messages; the future IPv69 stream transport
 * (next_header 2, "own HTTPS") can reuse this same library for its
 * handshake without depending on IPv69 code.
 *
 * Keys: the private key is a 32-byte seed; sk[64] = seed || pub
 * (TweetNaCl layout). The seed never leaves the device.
 */
#ifndef ED25519_H
#define ED25519_H

#include <stdint.h>
#include <stddef.h>

#define ED25519_SEED_LEN 32
#define ED25519_PUB_LEN  32
#define ED25519_SIG_LEN  64
#define ED25519_SK_LEN   64

/* Generate a new keypair. sk[0..31] = seed, sk[32..63] = pub.
 * Returns 0 on success, -1 on error. */
int ed25519_keypair(uint8_t sk[ED25519_SK_LEN], uint8_t pub[ED25519_PUB_LEN]);

/* Derive the public key from a fixed 32-byte seed (RFC 8032). */
int ed25519_seed_to_pub(uint8_t pub[ED25519_PUB_LEN],
                        const uint8_t seed[ED25519_SEED_LEN]);

/* Sign `n` bytes of `msg`; writes the 64-byte signature to `sig`.
 * Only the first 32 bytes of `sk` (the seed) are used; the public key
 * is derived internally. Returns 0 on success, -1 on error. */
int ed25519_sign(uint8_t sig[ED25519_SIG_LEN], const uint8_t *msg, size_t n,
                 const uint8_t sk[ED25519_SK_LEN]);

/* Verify `sig` over `n` bytes of `msg`. Returns 0 if valid, -1 if not. */
int ed25519_verify(const uint8_t *msg, size_t n,
                   const uint8_t sig[ED25519_SIG_LEN],
                   const uint8_t pub[ED25519_PUB_LEN]);

/* Load the device key from `path`, or generate + persist it on first run
 * (seed hex, mode 0600). On creation prints the public key so it can be
 * registered elsewhere. Returns 0 on success, -1 on error. */
int ed25519_keyfile_load_or_create(const char *path,
                                   uint8_t sk[ED25519_SK_LEN],
                                   uint8_t pub[ED25519_PUB_LEN]);

/* Default key path: $HOME/.ipv69/key (fallback /root/.ipv69/key). */
void ed25519_keyfile_default_path(char *out, size_t outsz);

/* SHA-512 (TweetNaCl crypto_hash). 64-byte digest. */
void ed25519_sha512(uint8_t out[64], const uint8_t *msg, size_t n);

/* Ed25519 pub (Edwards y) -> X25519 pub (Montgomery u): lets Ed25519
 * identities do static X25519 key exchange. */
void ed25519_pub_to_x25519(uint8_t out[32], const uint8_t ed_pub[32]);

/* HMAC-SHA512 (RFC 2104) built on ed25519_sha512. key may be any length
 * (block size 128); out[64]. Cheap enough for per-packet filters. */
void ed25519_hmac_sha512(uint8_t out[64], const uint8_t *msg, size_t n,
                         const uint8_t *key, size_t klen);

/* Poly1305 one-time authenticator (TweetNaCl crypto_onetimeauth):
 * 16-byte tag over msg with a 32-byte key — ~µs, the building block of
 * the WireGuard-style mac1 pre-auth filter and dgram authentication. */
void ed25519_poly1305(uint8_t out[16], const uint8_t *msg, size_t n,
                      const uint8_t key[32]);
int ed25519_poly1305_verify(const uint8_t tag[16], const uint8_t *msg,
                            size_t n, const uint8_t key[32]);

/* XSalsa20-Poly1305 secretbox (TweetNaCl crypto_secretbox): encrypts
   `n` bytes to `c`, authenticated. key[32], nonce[24]. open returns
   0 on success, -1 on auth failure.
   NOTE: c must hold n + 32 bytes (TweetNaCl pads 32 zeros in front
   of the message; the MAC lives in c[16..31]). */
void ed25519_secretbox(uint8_t *c, const uint8_t *m, size_t n,
                       const uint8_t nonce[24], const uint8_t key[32]);
int ed25519_secretbox_open(uint8_t *m, const uint8_t *c, size_t n,
                           const uint8_t nonce[24], const uint8_t key[32]);

/* X25519 (TweetNaCl crypto_scalarmult): ECDH. q = n * p, all 32B.
   base: q = n * basepoint (generates an ephemeral public key).
   Returns 0 on success, -1 on failure. */
int ed25519_scalarmult(uint8_t q[32], const uint8_t n[32], const uint8_t p[32]);
int ed25519_scalarmult_base(uint8_t q[32], const uint8_t n[32]);

/* secure RNG: getrandom() on POSIX, BCryptGenRandom on Windows.
 * Fills `n` bytes; never fails (loops on error). */
void randombytes(uint8_t *buf, uint64_t n);

#endif
