#include <stdlib.h>
#include <err.h>

static inline size_t parse_size(const char *s) {
  char *end;
  const size_t n = strtoul(s, &end, 0);
  if (*s == '\0' || *end != '\0')
    errx(EXIT_FAILURE, "ERROR: bad size_t: %s", s);
  return n;
}
