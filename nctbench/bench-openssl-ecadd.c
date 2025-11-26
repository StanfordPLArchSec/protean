#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/obj_mac.h>
#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include "shared.h"

int main(int argc, char *argv[]) {
  if (argc != 2)
    errx(EXIT_FAILURE, "usage: %s n", argv[0]);
  const size_t nreps = parse_size(argv[1]);

  EC_GROUP *group = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
  if (!group)
    errx(EXIT_FAILURE, "ERROR: EC_GROUP_new_by_curve_name");

  EC_POINT *P = EC_POINT_new(group);
  EC_POINT *Q = EC_POINT_new(group);
  EC_POINT *R = EC_POINT_new(group);
  if (!P || !Q || !R)
    errx(EXIT_FAILURE, "ERROR: EC_POINT_new");

  BN_CTX *ctx = BN_CTX_new();
  BIGNUM *x = BN_new();
  BIGNUM *y = BN_new();

  BN_rand_range(x, EC_GROUP_get0_order(group));
  BN_rand_range(y, EC_GROUP_get0_order(group));
  EC_POINT_mul(group, P, x, NULL, NULL, ctx);
  EC_POINT_mul(group, Q, y, NULL, NULL, ctx);

  for (size_t i = 0; i < nreps; ++i) {
    if (!EC_POINT_add(group, R, P, Q, ctx))
      errx(EXIT_FAILURE, "EC_POINT_add");
  }
}
