#include <stdlib.h>

int main() {
  void *p = malloc(100);
  p = realloc(p, 50);
  p = realloc(p, 150);
  free(p);
}
