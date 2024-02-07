#include <stdlib.h>

int main() {
  void *p = calloc(100, 100);
  p = reallocarray(p, 102, 102);
  p = reallocarray(p, 1, 1);
  free(p);
}
