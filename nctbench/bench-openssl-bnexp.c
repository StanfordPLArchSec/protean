#include <openssl/bn.h>
#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include "shared.h"

int main(int argc, char *argv[]) {
  if (argc != 3)
    errx(EXIT_FAILURE, "usage: %s nbits nreps", argv[0]);
  const size_t nbits = parse_size(argv[1]);
  const size_t nreps = parse_size(argv[2]);

  BN_CTX *ctx = BN_CTX_new();
  BIGNUM *a = BN_new();
  BIGNUM *p = BN_new();
  BIGNUM *m = BN_new();
  BIGNUM *r = BN_new();

  // Initialize a, p with random data.
  if (!BN_rand(a, nbits, 0, 0) ||
      !BN_rand(p, nbits, 0, 0) ||
      !BN_rand(m, nbits, 0, 0))
    errx(EXIT_FAILURE, "ERROR: BN_rand failed");
  BN_set_bit(m, 0); // Ensure the modulus is odd.

  for (size_t i = 0; i < nreps; ++i) {
    asm volatile ("int3");
    if (!BN_mod_exp(r, a, p, m, ctx))
      errx(EXIT_FAILURE, "ERROR: BN_mod_exp failed");
    asm volatile ("int3");
  }
}
