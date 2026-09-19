# ed25519 + TweetNaCl (vendorizado)

Snapshot de `lib/ed25519/` do repo do autor (IPv69): o wrapper `ed25519.h/.c` dele e o
TweetNaCl (domínio público, 844 linhas). Entra no runtime do Narval porque é a cripto que o
`baitnet` usa — Ed25519 (identidade/assinatura), X25519 (ECDHE), XSalsa20-Poly1305
(secretbox), SHA-512, HMAC-SHA512 e Poly1305 — e porque já é portátil nos dois alvos
(`getrandom` no POSIX, `BCryptGenRandom` no Windows, em `randombytes.c`).

**NÃO EDITAR AQUI.** Correções vão no repo de origem (`IPv69/lib/ed25519`) e o snapshot é
recopiado. O único ponto de contato com a linguagem é `../crypto_bridge.c` + o registro em
`include/backend/runtime/modules/crypto.def`.
