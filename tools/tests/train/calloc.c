#include <stdlib.h>
#include <stdio.h>

int main() {
  void *p = calloc(100, 100);
  printf("%p\n", p);
  free(p);
}
