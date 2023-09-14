#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <string>
#include <err.h>
#include <set>
#include <unordered_map>
#include <vector>
#include <numeric>
#include <map>

#include "Declassify.h"

struct Value {
  void *src; // instruction that last wrote to this entry (can be load if hit or store if hit/miss)
  bool declassified; // whether this is declassified
};

struct Stats {
  std::unordered_map<void *, unsigned long> classify_miss_store_hist; // store histogram
  std::map<std::pair<void *, void *>, unsigned long> classify_miss_rf_hist;
  unsigned long cold_misses = 0;
  unsigned long hits = 0;
  unsigned long classify_misses = 0;
};

using Memory = std::unordered_map<void *, Value>;


void handle_byte_access(void *inst, void *addr, bool read, bool declassified, Memory& mem, Stats& stats) {
  if (read) {
    // load
    if (declassified) {
      const auto it = mem.find(addr);
      if (it == mem.end()) {
	// cold miss, sicne no entry
	++stats.cold_misses;
      } else if (it->second.declassified) {
	// hit
	++stats.hits;
	it->second.src = inst;
      } else {
	++stats.classify_misses;
	++stats.classify_miss_store_hist[it->second.src];
	++stats.classify_miss_rf_hist[std::make_pair(it->second.src, inst)];
      }
    }
  } else {
    // store
    auto& v = mem[addr];
    v.src = inst;
    v.declassified = declassified;
  }
}


int main(int argc, char *argv[]) {
  FILE *f = stdin;
  
  Memory mem;
  Stats stats;

  Record record;
  while (fread(&record, sizeof record, 1, f)) {
    const bool read = record.isRead();
    const bool declassified = (record.mode & TT_MASK) == TT_DECLASSIFY;
    for (uint8_t i = 0; i < record.getSize(); ++i) {
      handle_byte_access(record.inst, record.addr, read, declassified, mem, stats);
    }
  }

  // Print out stats
  // compute total hits
  unsigned long misses = stats.cold_misses + stats.classify_misses;
  unsigned long combined = stats.hits + misses;
  printf("total %lu\n", combined);
  printf("hits %lu %.2f%%\n", stats.hits, static_cast<double>(stats.hits) / combined * 100);
  printf("misses %lu %.2f%%\n", misses, static_cast<double>(misses) / combined * 100);
  printf("cold-misses %lu %.2f%%\n", stats.cold_misses, static_cast<double>(stats.cold_misses) / combined * 100);
  printf("classify-misses %lu %.2f%%\n", stats.classify_misses, static_cast<double>(stats.classify_misses) / combined * 100);
  
  // Compute histogram
  std::vector<std::pair<void *, unsigned long>> classify_miss_store_hist {
    stats.classify_miss_store_hist.begin(),
    stats.classify_miss_store_hist.end()
  };
  std::sort(classify_miss_store_hist.begin(), classify_miss_store_hist.end(),
	    [] (const auto& p1, const auto& p2) {
	      return p1.second < p2.second;
	    });
  const unsigned long classify_miss_store_hist_total = std::accumulate(classify_miss_store_hist.begin(), classify_miss_store_hist.end(), 0UL,
		  [] (unsigned long acc, const auto& p) {
		    return acc + p.second;
		  });
  unsigned i = 0;
  for (auto it = classify_miss_store_hist.rbegin();
       it != classify_miss_store_hist.rend() && i < 100;
       ++it, ++i) {
    printf("classify-store-hist-%u %p %lu %.2f%%\n", i, it->first, it->second, static_cast<double>(it->second) / classify_miss_store_hist_total * 100);
  }

  // Compute rf histogram
  std::vector<std::pair<std::pair<void *, void *>, unsigned long>> classify_miss_rf_hist {
    stats.classify_miss_rf_hist.begin(), stats.classify_miss_rf_hist.end()
  };
  std::sort(classify_miss_rf_hist.begin(), classify_miss_rf_hist.end(),
	    [] (const auto& p1, const auto& p2) {
	      return p1.second < p2.second;
	    });
  const unsigned long classify_miss_rf_hist_total = std::accumulate(classify_miss_rf_hist.begin(), classify_miss_rf_hist.end(), 0UL,
								    [] (unsigned long acc, const auto& p) {
								      return acc + p.second;
								    });
  i = 0;
  for (auto it = classify_miss_rf_hist.rbegin();
       it != classify_miss_rf_hist.rend() && i < 100;
       ++it, ++i) {
    printf("classify-rf-hist-%u %p %p %lu %.2f%%\n", i, it->first.first, it->first.second, it->second, static_cast<double>(it->second) / classify_miss_rf_hist_total * 100);
  }
}
