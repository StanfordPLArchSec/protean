#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <stdlib.h>
#include <stdint.h>
#include <err.h>
#include <stdint.h>
#include "shared.h"

int main(int argc, char *argv[]) {
  if (argc != 3)
    errx(EXIT_FAILURE, "usage: %s msglen reps", argv[0]);

  const size_t msglen = parse_size(argv[1]);
  const size_t n = parse_size(argv[2]);

  uint8_t *msg = malloc(msglen);
  if (RAND_bytes(msg, msglen) != 1)
    errx(EXIT_FAILURE, "ERROR: failed to generate random bytes");
  uint8_t *out = malloc(msglen);

  static const uint8_t key[] = {
    11, 22, 33, 44, 55, 66, 77, 88, 99, 111, 122, 133, 144, 155, 166, 177,
    188, 199, 211, 222, 233, 244, 255, 0, 10, 20, 30, 40, 50, 60, 70, 80,
  };
  static const uint8_t nonce[] = {
    98, 76, 54, 32, 10, 0, 2, 4, 6, 8, 10, 12,
  };

  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  for (size_t i = 0; i < n; ++i) {
    EVP_EncryptInit_ex2(ctx, EVP_chacha20_poly1305(), key, nonce, NULL);

    int outlen;
    int tmplen;
    if (!EVP_EncryptUpdate(ctx, out, &outlen, msg, msglen) ||
        !EVP_EncryptFinal_ex(ctx, out + outlen, &tmplen))
      errx(EXIT_FAILURE, "ERROR: failed");
  }
}
