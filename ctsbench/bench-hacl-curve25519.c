#include <err.h>
#include <stdlib.h>
#include <stdint.h>
#include <Hacl_Curve25519_51.h>
#include "shared.h"

int main(int argc, char *argv[]) {
  if (argc != 2)
    errx(EXIT_FAILURE, "usage: %s n", argv[0]);

  const size_t n = parse_size(argv[1]);

#define KEY_BYTES 32
  uint8_t out[KEY_BYTES];
  static const uint8_t priv[KEY_BYTES] = {
    0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30,
    201, 203, 205, 207, 209, 211, 213, 215, 217, 219, 221, 223, 225, 227, 229, 231,
  };
  static const uint8_t pub[KEY_BYTES] = {
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
    100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115,    
  };

  for (size_t i = 0; i < n; ++i)
    Hacl_Curve25519_51_ecdh(out, priv, pub);
}
