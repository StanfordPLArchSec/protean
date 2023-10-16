#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <vector>
#include <optional>
#include <array>
#include <map>
#include <cstring>
#include <err.h>

using Score = float;

struct Pattern {
  std::vector<uint8_t> data;
  unsigned repcnt;

  Pattern(unsigned repcnt, uint8_t *base, unsigned patlen):
    data(base, base + patlen), repcnt(repcnt) {}

  Score score() const {
    return std::pow(static_cast<Score>(repcnt), 2) / data.size();
  }

  size_t size() const {
    return data.size() * repcnt;
  }
};

static std::optional<Pattern> check_pattern(uint8_t *base, unsigned patlen, size_t maxsize) {
  if (maxsize < patlen)
    return std::nullopt;

  unsigned repcnt = 1;
  while ((repcnt + 1) * patlen <= maxsize) {
    if (std::memcmp(base, base + repcnt * patlen, patlen) != 0)
      break;
    ++repcnt;
  }

  return Pattern(repcnt, base, patlen);
}

static std::optional<Pattern> find_best_pattern_from(uint8_t *base, unsigned maxpatlen, size_t maxsize) {
  std::optional<Pattern> best_pattern;
  
  for (unsigned patlen = 1; patlen <= maxpatlen; ++patlen) {
    if (const auto pattern = check_pattern(base, patlen, maxsize)) {
      if (!best_pattern || pattern->score() > best_pattern->score())
	best_pattern = pattern;
    }
  }

  return best_pattern;
}

static std::vector<Pattern> find_patterns(uint8_t *base, size_t size, unsigned maxpatlen) {
  std::vector<Pattern> patterns;
  while (size > 0) {
    if (const auto pattern = find_best_pattern_from(base, maxpatlen, size)) {
      patterns.push_back(*pattern);
      size -= pattern->size();
      base += pattern->size();
    }
  }

  return patterns;
}

static void usage(FILE *f, const char *prog) {
  fprintf(f, "usage: %s max-pattern-length [file]\n", prog);
}

int main(int argc, char *argv[]) {
  if (argc < 2 || argc > 3) {
    usage(stderr, argv[0]);
    return EXIT_FAILURE;
  }

  const unsigned max_pattern_length = atoi(argv[1]);
  FILE *f = stdin;
  if (argc == 3) {
    const char *path = argv[2];
    if ((f = fopen(path, "r")) == nullptr)
      err(EXIT_FAILURE, "fopen");
  }

  // Copy all of file.
  std::vector<uint8_t> mem;
  {
    std::array<uint8_t, 4096> buf;
    size_t bytes;
    while ((bytes = fread(buf.data(), 1, buf.size(), f)) > 0) {
      std::copy_n(buf.begin(), bytes, std::back_inserter(mem));
    }
    if (ferror(f))
      err(EXIT_FAILURE, "fread");
  }

  const size_t nonzero_bytes = mem.size() - std::count(mem.begin(), mem.end(), 0);

  // Do pattern matching.
  const auto patterns = find_patterns(mem.data(), mem.size(), max_pattern_length);

#if 0
  for (Pattern pattern : patterns) {
    printf("%u %zu %zu ", pattern.repcnt, pattern.data.size(), pattern.size());
    for (uint8_t byte : pattern.data)
      printf("%02hhx", byte);
    printf("\n");
  }
#endif

  // Calculate number of bytes occupied by each pattern total.
  struct Entry {
    size_t total_bytes = 0;
    size_t groups = 0;
  };
  std::map<std::vector<uint8_t>, Entry> totals;
  for (const Pattern& pattern : patterns) {
    Entry& e = totals[pattern.data];
    e.total_bytes += pattern.size();
    ++e.groups;
  }

  printf("total-bytes pattern-length groups average-repetitions pattern\n");
  for (const auto& [pattern_data, e] : totals) {
    printf("%zu %.2f%% %zu %zu %zu ", e.total_bytes * 8, static_cast<float>(e.total_bytes) / mem.size() * 100, pattern_data.size() * 8, e.groups, e.total_bytes / e.groups / pattern_data.size());
    for (uint8_t byte : pattern_data) {
      printf("%02hhx", byte);
    }
    printf("\n");
  }
}
