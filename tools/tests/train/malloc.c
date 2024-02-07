#include <stdlib.h>

int main() {
  void *p = malloc(16);
  free(p);
}
