#pragma once

#include <memory>
#include <vector>
#include <array>
#include <ostream>

#include "pin.H"

template <typename T>
constexpr T div_up(T numer, T denom) {
  return (numer + denom - 1) / denom;
}

template <typename T, unsigned ValueBits_, unsigned PageBits_>
class ShadowMemory {
public:
  static inline constexpr unsigned AddrBits = 48;
  
  static inline constexpr unsigned ValueBitLo = 0;
  static inline constexpr unsigned ValueBits = ValueBits_;
  static inline constexpr unsigned ValueBitHi = ValueBitLo + ValueBits;
  static inline constexpr unsigned ValueSize = 1 << ValueBits;
  
  static inline constexpr unsigned PageBitLo = ValueBitHi;
  static inline constexpr unsigned PageBits = PageBits_;
  static inline constexpr unsigned PageBitHi = PageBitLo + PageBits;
  static inline constexpr unsigned PageSize = 1 << PageBits;
  
  static inline constexpr unsigned TotalIndexBits = AddrBits - PageBitHi;
  
  static inline constexpr unsigned LowerIndexBitLo = PageBitHi;
  static inline constexpr unsigned LowerIndexBits = TotalIndexBits / 2;
  static inline constexpr unsigned LowerIndexBitHi = PageBitHi + LowerIndexBits;
  static inline constexpr unsigned LowerIndexSize = 1 << LowerIndexBits;
  
  static inline constexpr unsigned UpperIndexBitLo = LowerIndexBitHi;
  static inline constexpr unsigned UpperIndexBitHi = AddrBits;
  static inline constexpr unsigned UpperIndexBits = UpperIndexBitHi - UpperIndexBitLo;
  static inline constexpr unsigned UpperIndexSize = 1 << UpperIndexBits;

  ShadowMemory(const T& init) {
    std::fill(clean_page.begin(), clean_page.end(), init);
  }

  T& operator[](ADDRINT addr) {
    const ADDRINT upper_idx = (addr >> UpperIndexBitLo) & (UpperIndexSize - 1);
    const ADDRINT lower_idx = (addr >> LowerIndexBitLo) & (LowerIndexSize - 1);
    const ADDRINT page_idx = (addr >> PageBitLo) & (PageSize - 1);

    std::unique_ptr<LowerPageTable>& lower_ptable = mem[upper_idx];
    if (!lower_ptable)
      lower_ptable = std::make_unique<LowerPageTable>();

    std::unique_ptr<Page>& page = (*lower_ptable)[lower_idx];
    if (!page)
      page = std::make_unique<Page>(clean_page);

    return (*page)[page_idx];
  }

  void for_each(std::function<void (ADDRINT, const T&)> func) const {
    for (unsigned upper_idx = 0; upper_idx < (1 << UpperIndexBits); ++upper_idx) {
      if (const auto& lower_ptable = mem.at(upper_idx)) {
	for (unsigned lower_idx = 0; lower_idx < (1 << LowerIndexBits); ++lower_idx) {
	  if (const auto& page = (*lower_ptable)[lower_idx]) {
	    for (unsigned off = 0; off < (1 << PageBits); ++off) {
	      const ADDRINT addr =
		(upper_idx << UpperIndexBitLo) |
		(lower_idx << LowerIndexBitLo) |
		(off << ValueBitLo);
	      func(addr, (*page)[off]);
	    }
	  }
	}
      }
    }
  }
  
private:
  using Page = std::array<T, 1 << PageBits>;
  Page clean_page;
  using LowerPageTable = std::array<std::unique_ptr<Page>, 1 << LowerIndexBits>;
  using UpperPageTable = std::array<std::unique_ptr<LowerPageTable>, 1 << UpperIndexBits>;
  
  UpperPageTable mem;
};

  
class ShadowDeclassificationTable {
public:
  ShadowDeclassificationTable():
    shadow_declassified(false),
    shadow_declassified_store(0),
    shadow_store_inst_classify_miss(0)
  {}
      
    
  bool checkDeclassified(ADDRINT eff_addr, unsigned eff_size) {
    bool *ptr = &shadow_declassified[eff_addr];
    const bool declassified = std::reduce(ptr, ptr + eff_size, true, std::logical_and<bool>());
    if (!declassified) {
      if (ADDRINT store = shadow_declassified_store[eff_addr]) {
	++shadow_store_inst_classify_miss[store];
	++stat_classify_misses;
      } else {
	++stat_cold_misses; 
      }
    } else {
      ++stat_hits;
    }
    return declassified;
  }
    
  void setDeclassified(ADDRINT eff_addr, unsigned eff_size) {
    bool *ptr = &shadow_declassified[eff_addr];
    std::fill(ptr, ptr + eff_size, true);
  }

  void setClassified(ADDRINT eff_addr, unsigned eff_size, ADDRINT store_inst) {
    bool *ptr = &shadow_declassified[eff_addr];
    std::fill(ptr, ptr + eff_size, false);
    ADDRINT *store_ptr = &shadow_declassified_store[eff_addr];
    std::fill(store_ptr, store_ptr + eff_size, store_inst);
  }

  void printStats(std::ostream& os) {
    os << "hits " << stat_hits << "\n"
       << "cold-misses " << stat_cold_misses << "\n"
       << "classify-misses " << stat_classify_misses << "\n";
    std::vector<std::pair<ADDRINT, unsigned long>> stores;
    shadow_store_inst_classify_miss.for_each([&] (ADDRINT store_inst, unsigned long count) {
      stores.emplace_back(store_inst, count);
    });
    std::sort(stores.begin(), stores.end(), [] (const auto& p1, const auto& p2) -> bool {
      return p2.second < p1.second;
    });
    for (unsigned i = 0; i < 10 && i < stores.size(); ++i) {
      os << "classify-store-hist-" << i << " " << stores[i].first << " " << stores[i].second << "\n";
    }
  }
    
private:
  ShadowMemory<bool, 0, 12> shadow_declassified;
  ShadowMemory<ADDRINT, 0, 12> shadow_declassified_store;
  ShadowMemory<unsigned long, 0, 12> shadow_store_inst_classify_miss;
  unsigned long stat_classify_misses = 0;
  unsigned long stat_cold_misses = 0;
  unsigned long stat_hits = 0;
#if 0
  ShadowMemory<int, 12, 0> shadow_declassified_pages(shadow_declassified_pages.ValueSize);
#endif
};


// For now, hard-code three levels.
// Consider varying the number in the future.
template <unsigned LineSizeBits_, unsigned TableSizeBits_, unsigned NumTables_>
class ParallelDeclassificationTable {
public:
  static inline constexpr unsigned NumTables = NumTables_;
  static inline constexpr unsigned LineSizeBits = LineSizeBits_;
  static inline constexpr size_t LineSize = 1 << LineSizeBits;
  static inline constexpr unsigned TableSizeBits = TableSizeBits_;
  static inline constexpr size_t TableSize = 1 << TableSizeBits;

  ParallelDeclassificationTable() {
    std::fill(stats_evictions.begin(), stats_evictions.end(), 0UL);
    std::fill(stats_upgrades.begin(), stats_upgrades.end(), 0UL);
  }

  bool checkDeclassified(ADDRINT addr, unsigned size) {
    for (Table& table : tables) {
      const unsigned line_off = addr & (LineSize - 1);
      const ADDRINT tag = addr >> LineSizeBits;
      const unsigned line_idx = tag & (TableSize - 1);
      const Line& line = table[line_idx];
      if (line && line.tag == tag) {
	assert(line_off + size <= line.entries.size());
	const auto it = line.entries.begin() + line_off;
	const bool declassified = std::reduce(it, it + size, true, std::logical_and<bool>());
	return declassified;
      }

      // next
      addr = tag;
      size = 1;
    }
    return false;
  }

  void setDeclassified(ADDRINT addr, unsigned size) {
    for (unsigned table_idx = 0; table_idx < NumTables; ++table_idx) {
      Table& table = tables[table_idx];
      const unsigned line_off = addr & (LineSize - 1);
      const ADDRINT tag = addr >> LineSizeBits;
      const unsigned line_idx = tag & (TableSize - 1);
      Line& line = table[line_idx];
      if (!(line && line.tag == tag)) {
	if (line.valid) {
	  ++stats_evictions[table_idx];
	} else {
	  line.valid = true;
	}
	line.tag = tag;
	std::fill(line.entries.begin(), line.entries.end(), false);
      }

      std::fill_n(line.entries.begin() + line_off, size, true);

      if (table_idx == NumTables - 1)
	break;

      const bool all_declassified =
	std::reduce(line.entries.begin(), line.entries.end(), true, std::logical_and<bool>());

      if (!all_declassified)
	break;

      line.valid = false;
      ++stats_upgrades[table_idx];

      addr = tag;
      size = 1;
    }
  }

  void setClassified(ADDRINT addr, unsigned size, ADDRINT) {
    for (Table& table : tables) {
      const unsigned line_off = addr & (LineSize - 1);
      const ADDRINT tag = addr >> LineSizeBits;
      const unsigned line_idx = tag & (TableSize - 1);
      Line& line = table[line_idx];

      // Do we have a match?
      if (line && line.tag == tag) {
	std::fill_n(line.entries.begin() + line_off, size, false);
	break;
      }

      // Update "address" and "size".
      addr = tag;
      size = 1;
    }
  }

  void clear() {
    for (auto& table : tables)
      for (auto& line : table)
	line.valid = false;
  }

  void printStats(std::ostream& os) {
    for (unsigned table_idx = 0; table_idx < NumTables; ++table_idx) {
      os << "table" << table_idx << ".evictions " << stats_evictions[table_idx] << "\n";
      if (table_idx < NumTables - 1)
	os << "table" << table_idx << ".upgrades " << stats_upgrades[table_idx] << "\n";
    }
  }

private:
  struct Line {
    bool valid = false;
    ADDRINT tag;
    std::array<bool, LineSize> entries;

    operator bool() const { return valid; }
  };
  using Table = std::array<Line, TableSize>;
  std::array<Table, NumTables> tables;
  std::array<unsigned long, NumTables> stats_evictions;
  std::array<unsigned long, NumTables-1> stats_upgrades;
};

