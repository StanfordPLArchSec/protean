#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <stdlib.h>
#include <err.h>
#include "shared.h"

int main(int argc, char *argv[]) {
  if (argc != 2)
    errx(EXIT_FAILURE, "usage: %s n", argv[0]);
  const size_t n = parse_size(argv[1]);

  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, NULL);
  if (!ctx)
    errx(EXIT_FAILURE, "ERROR: failed to create context");

  if (EVP_PKEY_keygen_init(ctx) <= 0)
    errx(EXIT_FAILURE, "ERROR: keygen init failed");

  for (size_t i = 0; i < n; ++i) {
    EVP_PKEY *pkey = NULL;
    if (EVP_PKEY_keygen(ctx, &pkey) <= 0)
      errx(EXIT_FAILURE, "ERROR: keygen failed");
    EVP_PKEY_free(pkey);
  }
}
