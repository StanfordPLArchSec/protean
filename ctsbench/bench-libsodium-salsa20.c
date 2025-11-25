#include <sodium.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <err.h>

#include "shared.h"

int main(int argc, char *argv[]) {
  if (sodium_init() < 0)
    errx(EXIT_FAILURE, "FAIL: libsodium couldn't be initialized");

  if (argc != 3)
    errx(EXIT_FAILURE, "usage: %s chunksize reps\n", argv[0]);

  const size_t message_len = parse_size(argv[1]);
  const size_t n = parse_size(argv[2]);


  unsigned char key[crypto_stream_salsa20_KEYBYTES];
  unsigned char nonce[crypto_stream_salsa20_NONCEBYTES];

  unsigned char *message = malloc(message_len);
  unsigned char *ciphertext = malloc(message_len);
  unsigned char *decrypted = malloc(message_len);

  // Fill key and nonce with random values
  randombytes_buf(message, message_len);
  randombytes_buf(key, sizeof key);
  randombytes_buf(nonce, sizeof nonce);

  for (size_t i = 0; i < n; ++i) {
    // Encrypt
    crypto_stream_salsa20_xor(ciphertext, (const unsigned char *) message, message_len, nonce, key);
    
    // Decrypt (same as encrypt again with same key/nonce)
    crypto_stream_salsa20_xor(decrypted, ciphertext, message_len, nonce, key);
    
    // Check
    if (memcmp(message, decrypted, message_len)) {
      fprintf(stderr, "FAIL: decrypted message does not match\n");
      return EXIT_FAILURE;
    }
  }
}
