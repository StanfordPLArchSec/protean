#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <stdlib.h>
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

  for (size_t i = 0; i < n; ++i) {
    uint8_t md[SHA256_DIGEST_LENGTH];
    SHA256(msg, msglen, md);
  }
}
