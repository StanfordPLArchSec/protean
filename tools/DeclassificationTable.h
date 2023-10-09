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

  bool checkDeclassified(ADDRINT eff_addr, unsigned eff_size) {
    ADDRINT addr = eff_addr;
    unsigned size = eff_size;
    bool declassified = false;
    for (unsigned table_idx = 0; table_idx < NumTables; ++table_idx) {
      const unsigned line_off = addr & (LineSize - 1);
      const ADDRINT tag = addr >> LineSizeBits;
      const unsigned line_idx = tag & (TableSize - 1);
      const Line& line = tables[table_idx][line_idx];
      if (line && line.tag == tag) {
	declassified =
	  std::reduce(line.entries.begin() + line_off,
		      line.entries.begin() + line_off + size,
		      true,
		      std::logical_and<bool>());
	break;
      }

      // Update
      addr = tag;
      size = div_up<unsigned>(size, LineSize);
    }
    if (declassified) {
      ++stat_hits; 
    } else {
      ++stat_misses;
    }
    return false;
  }

  void setDeclassified(ADDRINT eff_addr, unsigned eff_size) {
    if (checkDeclassified(eff_addr, eff_size))
      return;

    // At this point, we know that there are no entries for which the bit is set,
    // so we can safely allocate a bottom-level line.
    const ADDRINT bottom_tag = eff_addr >> LineSizeBits;
    const ADDRINT line_idx = bottom_tag & (TableSize - 1);
    const ADDRINT line_off = eff_addr & (LineSize - 1);
    Line& bottom_line = tables[0][line_idx];

    // Force-allocate a line if necessary.
    if (!(bottom_line && bottom_line.tag == bottom_tag)) {
      bottom_line.valid = true;
      bottom_line.tag = bottom_tag;
      std::fill(bottom_line.entries.begin(), bottom_line.entries.end(), false);
    }
      
    // Set declassified bits.
    std::fill_n(bottom_line.entries.begin() + line_off, eff_size, true);

    // Try to promote the line.
    tryPromoteLine(bottom_line, 0);
  }

  void setClassified(ADDRINT eff_addr, unsigned eff_size, ADDRINT) {
    // Use naive strategy for now:
    // Simply zero out any corresponding bits at any level.

    ADDRINT addr = eff_addr;
    unsigned size = eff_size;
    for (unsigned table_idx = 0; table_idx < NumTables; ++table_idx) {
      const unsigned line_off = addr & (LineSize - 1);
      const ADDRINT tag = addr >> LineSizeBits;
      const unsigned line_idx = tag & (TableSize - 1);

      Line& line = tables[table_idx][line_idx];

      // Do we have a match?
      if (line && line.tag == tag) {
	std::fill_n(line.entries.begin() + line_off, size, false);
	return;
      }

      // Update "address" and "size".
      addr = tag;
      size = div_up<unsigned>(size, LineSize);
    }
  }

  void printStats(std::ostream& os) const {
    os << "hits " << stat_hits << "\n"
       << "misses " << stat_misses << "\n";
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
  unsigned long stat_hits;
  unsigned long stat_misses;

  Line& getLine(ADDRINT eff_addr, unsigned table_idx) {
    const ADDRINT tag = eff_addr >> ((table_idx + 1) * LineSizeBits);
    
  }

  void tryPromoteLine(Line& line, unsigned table_idx) {
    if (table_idx >= NumTables)
      return;
    
    const bool all_declassified =
      std::reduce(line.entries.begin(), line.entries.end(), true, std::logical_and<bool>());
    if (!all_declassified)
      return;

    // Invalidate lower line.
    line.valid = false;
    

    // Get (and maybe allocate) upper line.
    const unsigned upper_table_idx = table_idx + 1;
    const ADDRINT upper_tag = line.tag >> LineSizeBits;
    const ADDRINT upper_line_idx = upper_tag & (TableSize - 1);
    Line& upper_line = tables[upper_table_idx][upper_line_idx];
    if (!(upper_line && upper_line.tag == upper_tag)) {
      upper_line.valid = true;
      upper_line.tag = upper_tag;
      std::fill(upper_line.entries.begin(), upper_line.entries.end(), false);
    }

    // Set corresponding bit in upper line.
    const ADDRINT upper_line_off = line.tag & (LineSize - 1);
    upper_line.entries[upper_line_off] = true;

    // Try to promote the upper line.
    tryPromoteLine(upper_line, upper_table_idx);
  }
};

