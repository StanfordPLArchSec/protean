#include <stdlib.h>

int val = 0;

int main() {
  long *p = malloc(16);
  *p = p[1];
  free(p);
}
