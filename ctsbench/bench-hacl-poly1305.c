#include <err.h>
#include <sys/random.h>
#include <stdlib.h>
#include <stdint.h>
#include <Hacl_Poly1305_32.h>
#include "shared.h"

int main(int argc, char *argv[]) {
  if (argc != 3)
    errx(EXIT_FAILURE, "usage: %s msglen n\n", argv[0]);

  const size_t msglen = parse_size(argv[1]);
  const size_t n = parse_size(argv[2]);

#define TAG_BYTES 16
#define KEY_BYTES 16
  uint8_t tag[TAG_BYTES];
  uint8_t key[KEY_BYTES];
  uint8_t *msg = malloc(msglen);
  if (getrandom(key, sizeof key, 0) != sizeof key ||
      getrandom(msg, msglen, 0) != msglen)
    errx(EXIT_FAILURE, "ERROR: getrandom");

  for (size_t i = 0; i < n; ++i)
    Hacl_Poly1305_32_poly1305_mac(tag, msglen, msg, key);
}
