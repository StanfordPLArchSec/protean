#include <openssl/dh.h>
#include <openssl/bn.h>
#include <stdio.h>
#include <stdlib.h>
#include "shared.h"

int main(int argc, char *argv[]) {
  if (argc != 3)
    errx(EXIT_FAILURE, "usage: %s nbits nreps", argv[0]);
  const size_t nbits = parse_size(argv[1]);
  const size_t nreps = parse_size(argv[2]);

  BN_CTX *ctx = BN_CTX_new();
  if (!ctx)
    errx(EXIT_FAILURE, "ERROR: BN_CTX_new");

  BIGNUM *p = BN_new();
  if (!BN_generate_prime_ex(p, nbits, 1, NULL, NULL, NULL))
    errx(EXIT_FAILURE, "BN_generate_prime_ex failed");

  BIGNUM *g = BN_new();
  BN_set_word(g, 2);

  DH *dh1 = DH_new();
  DH *dh2 = DH_new();
  if (!DH_set0_pqg(dh1, BN_dup(p), NULL, BN_dup(g)) ||
      !DH_set0_pqg(dh2, BN_dup(p), NULL, BN_dup(g)))
    errx(EXIT_FAILURE, "ERROR: DH_set0_pqg");

  if (!DH_generate_key(dh1) || !DH_generate_key(dh2))
    errx(EXIT_FAILURE, "ERROR: DH_generate_key");

  const BIGNUM *pub1, *pub2;
  DH_get0_key(dh1, &pub1, NULL);
  DH_get0_key(dh2, &pub2, NULL);

  unsigned char *buf = malloc(DH_size(dh1));
  if (!buf)
    err(EXIT_FAILURE, "malloc");

  for (size_t i = 0; i < nreps; ++i) {
    const int len = DH_compute_key(buf, pub2, dh1);
    if (len <= 0)
      errx(EXIT_FAILURE, "ERROR: DH_compute_key");
  }
}
