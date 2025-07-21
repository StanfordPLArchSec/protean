#include <sodium.h>

#include "shared.h"

int main(int argc, char *argv[]) {
  if (sodium_init() < 0)
    errx(EXIT_FAILURE, "FAIL: libsodium couldn't be initialized");

  if (argc != 3)
    errx(EXIT_FAILURE, "usage: %s msglen reps\n", argv[0]);

  const size_t msglen = parse_size(argv[1]);
  const size_t n = parse_size(argv[2]);

  uint8_t *msg = malloc(msglen);

  randombytes_buf(msg, msglen);

  for (size_t i = 0; i < n; ++i) {
    uint8_t h[crypto_hash_BYTES];
    crypto_hash_sha256(h, msg, msglen);
  }
}
