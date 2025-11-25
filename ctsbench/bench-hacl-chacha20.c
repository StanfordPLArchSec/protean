#include <sys/random.h>
#include <err.h>
#include <stdlib.h>
#include <stdint.h>
#include <Hacl_Chacha20.h>
#include "shared.h"

int main(int argc, char *argv[]) {
  if (argc != 3)
    errx(EXIT_FAILURE, "usage: %s msglen reps", argv[0]);

  const size_t msglen = parse_size(argv[1]);
  const size_t n = parse_size(argv[2]);

  uint8_t *msg = malloc(msglen);
  uint8_t *out = malloc(msglen);
  if (getrandom(msg, msglen, 0) != msglen)
    errx(EXIT_FAILURE, "ERROR: getrandom");

  static const uint8_t key[] = {
    11, 22, 33, 44, 55, 66, 77, 88, 99, 111, 122, 133, 144, 155, 166, 177,
    188, 199, 211, 222, 233, 244, 255, 0, 10, 20, 30, 40, 50, 60, 70, 80,
  };
  static const uint8_t nonce[] = {98, 76, 54, 32, 10, 0, 2, 4, 6, 8, 10, 12};

  for (size_t i = 0; i < n; ++i)
    Hacl_Chacha20_chacha20_encrypt(msglen, out, msg, key, nonce, 0);
}
