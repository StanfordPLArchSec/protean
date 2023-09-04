#include <iostream>
#include <cstdio>
#include <string>
#include <err.h>
#include <memory>
#include <iomanip>
#include <cstdlib>

#include "decltab.hh"

int main(int argc, char *argv[]) {
  if (argc < 2)
    return EXIT_FAILURE;
  const std::string decltab_type = argv[1];
  std::unique_ptr<DeclTab> decltab;
  if (decltab_type == "lrubyte" || decltab_type == "randombyte" || decltab_type == "sizeaware") {
    if (argc != 3)
      return EXIT_FAILURE;
    std::size_t len;
    const std::string num_entries_str = argv[2];
    const unsigned long num_entries = std::stoul(num_entries_str, &len);
    if (decltab_type == "lrubyte") {
      decltab = std::make_unique<LRUByteDeclTab>(num_entries);
    } else if (decltab_type == "randombyte") {
      decltab = std::make_unique<RandomByteDeclTab>(num_entries);
    } else if (decltab_type == "sizeaware") {
      decltab = std::make_unique<SizeAwareDeclTab>(num_entries);
    } else if (decltab_type == "aligned-lrubyte") {
      decltab = std::make_unique<AlignedDeclTab<LRUByteDeclTab>>(num_entries);
    } else if (decltab_type == "shadow") {
      std::unique_ptr<UnsizedDeclTab> shadow_dt = std::make_unique<ShadowDeclTab>(num_entries);
      decltab = std::make_unique<ConvertSizeDeclTab>(1, std::move(shadow_dt));
    }

  } else if (decltab_type == "cachebyte") {
    unsigned line_size, num_lines;
    if (std::sscanf(argv[2], "line_size=%u", &line_size) != 1 ||
	std::sscanf(argv[3], "num_lines=%u", &num_lines) != 1)
      errx(EXIT_FAILURE, "invalid arguments to %s", decltab_type.c_str());
    std::unique_ptr<UnsizedDeclTab> cache_dt = std::make_unique<CacheDeclTab>(line_size, num_lines);
    decltab = std::make_unique<ConvertSizeDeclTab>(1, std::move(cache_dt));
  } else {
    errx(EXIT_FAILURE, "unknown decltab type: %s", decltab_type.c_str());
  }
  
  std::string line;
  // 114000: global: decltab-access: action=classify addr=0x363c68 size=8
  unsigned long hits, misses;
  hits = misses = 0;
  while (std::getline(std::cin, line)) {
    char action_cstr[256];
    Addr base;
    unsigned size;
    if (std::sscanf(line.c_str(), "%*u: global: decltab-access: action=%s addr=0x%lx size=%u",
		    action_cstr, &base, &size) != 3)
      errx(EXIT_FAILURE, "invalid line: %s", line.c_str());
    const std::string action(action_cstr);
    if (action == "check") {
      if (decltab->checkDeclassified(base, size)) {
	++hits;
      } else {
	++misses;
      }
    } else if (action == "declassify") {
      decltab->setDeclassified(base, size);
    } else if (action == "classify") {
      decltab->setClassified(base, size);
    } else {
      errx(EXIT_FAILURE, "invalid action '%s'", action_cstr);
    }
  }

  const double miss_rate = (double) misses / (double) (misses + hits) * 100;
  std::cout << "hits " << hits << "\n"
	    << "misses " << misses << "\n"
	    << "miss-rate " << std::setprecision(3) << miss_rate << "\n";
}
