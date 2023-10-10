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

  using Page = std::array<T, 1 << PageBits>;
  
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

  void for_each_page(std::function<void (ADDRINT, const Page&)> func) const {
    for (unsigned upper_idx = 0; upper_idx < (1 << UpperIndexBits); ++upper_idx) {
      if (const auto& lower_ptable = mem.at(upper_idx)) {
	for (unsigned lower_idx = 0; lower_idx < (1 << LowerIndexBits); ++lower_idx) {
	  if (const auto& page = (*lower_ptable)[lower_idx]) {
	    const ADDRINT addr = (upper_idx << UpperIndexBitLo) | (lower_idx << LowerIndexBitLo);
	    func(addr, *page);
	  }
	}
      }
    }
  }
  
private:
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

  void printDesc(std::ostream& os) {
    os << "Configuration: shadow memory\n";
  }

  void dumpMem(std::vector<uint8_t>& mem) const {
    shadow_declassified.for_each([&mem] (ADDRINT addr, bool) {
      uint8_t byte = 0;
      PIN_SafeCopy(&byte, (const VOID *) addr, 1);
      mem.push_back(byte);
    });
  }

  void dumpTaint(std::vector<uint8_t>& mem) const {
    shadow_declassified.for_each_page([&mem] (ADDRINT, const auto& bv) {
      for (unsigned i = 0; i < bv.size(); i += 8) {
	uint8_t mask = 0;
	for (unsigned j = 0; j < 8; ++j) {
	  mask <<= 1;
	  if (bv[i + j])
	    mask |= 1;
	}
	mem.push_back(mask);
      }
    });
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





template <unsigned NumTables_, std::array<unsigned, NumTables_> LineSize_, std::array<unsigned, NumTables_> TableSize_, std::array<unsigned, NumTables_> Associativity_>
class ParallelDeclassificationTable {
public:
  static constexpr unsigned NumTables() { return NumTables_; }
  static_assert(NumTables() >= 2, "");
  static constexpr unsigned LineSize(unsigned table_idx) {
    const unsigned line_size = LineSize_[table_idx];
    assert((line_size & (line_size - 1)) == 0);
    return line_size;
  }
  static constexpr unsigned TableSize(unsigned table_idx) { return TableSize_[table_idx]; }
  static constexpr unsigned Associativity(unsigned table_idx) { return Associativity_[table_idx]; }
  static constexpr unsigned TableRows(unsigned table_idx) {
    assert(TableSize(table_idx) % Associativity(table_idx) == 0);
    const unsigned table_rows = TableSize(table_idx) / Associativity(table_idx);
    assert((table_rows & (table_rows - 1)) == 0);
    return table_rows;
  }

private:
  struct Line {
    bool valid = false;
    ADDRINT tag;
    std::vector<bool> bv;

    Line(unsigned line_size): valid(false), bv(line_size) {}
    void assign(ADDRINT newtag, bool bv_init) {
      assert(tag != newtag);
      valid = true;
      tag = newtag;
      std::fill(bv.begin(), bv.end(), bv_init);
    }

    bool allSet() const {
      return std::reduce(bv.begin(), bv.end(), true, std::logical_and<bool>());
    }

    bool anySet() const {
      return std::reduce(bv.begin(), bv.end(), false, std::logical_or<bool>());
    }

    bool allReset() const {
      return !anySet();
    }

    bool allSet(unsigned idx, unsigned count) const {
      const auto it = bv.begin() + idx;
      return std::reduce(it, it + count, true, std::logical_and<bool>());
    }
  };

  class Row {
  public:
    using Tick = unsigned;
    using Lines = std::vector<Line>;
    Row(unsigned num_lines, unsigned line_size): lines(num_lines, Line(line_size)), used(num_lines, 0) {}
    Lines::const_iterator begin() const { return lines.begin(); }
    Lines::const_iterator end() const { return lines.end(); }
    Lines::iterator begin() { return lines.begin(); }
    Lines::iterator end() { return lines.end(); }

    // May evict an existing one.
    // Requires that no line with tag exists.
    Lines::iterator allocateLine(unsigned long& stats_evictions) {
      Tick min_tick = std::numeric_limits<Tick>::max();
      typename Lines::iterator min_it;
      for (unsigned idx = 0; idx < lines.size(); ++idx) {
	auto it = lines.begin() + idx;
	if (!it->valid)
	  return it;
	if (used[idx] <= min_tick) {
	  min_tick = used[idx];
	  min_it = it;
	}
      }
      ++stats_evictions;
      return min_it;
    }

    void markUsed(typename Lines::iterator it) {
      const unsigned idx = it - lines.begin();
      used[idx] = ++tick;
    }
    
  private:
    Lines lines;
    std::vector<Tick> used;
    Tick tick = 0;
  };

  class Table {
  public:
    Table(unsigned line_size, unsigned table_rows, unsigned table_cols):
      rows(table_rows, Row(table_cols, line_size)) {}
    const Row& operator[](unsigned row_idx) const { return rows[row_idx]; }
    Row& operator[](unsigned row_idx) { return rows[row_idx]; }
  private:
    std::vector<Row> rows;
  };
public:
  
  ParallelDeclassificationTable() {
    for (unsigned table_idx = 0; table_idx < NumTables(); ++table_idx) {
      tables.emplace_back(LineSize(table_idx), TableRows(table_idx), Associativity(table_idx));
      std::fill(stats_upgrades.begin(), stats_upgrades.end(), 0UL);
      std::fill(stats_evictions.begin(), stats_evictions.end(), 0UL);
      std::fill(stats_downgrades.begin(), stats_downgrades.end(), 0UL);
    }
  }

  bool checkDeclassified(ADDRINT addr, unsigned size, bool mark_used = true) {
    for (unsigned table_idx = 0; table_idx < NumTables(); ++table_idx) {
      Table& table = tables[table_idx];
      const unsigned line_off = addr & (LineSize(table_idx) - 1);
      const ADDRINT tag = addr / LineSize(table_idx);
      const unsigned row_idx = tag & (TableRows(table_idx) - 1);
      Row& row = table[row_idx];

      for (auto row_it = row.begin(); row_it != row.end(); ++row_it) {
	const Line& line = *row_it;
	if (line.valid && line.tag == tag) {
	  if (line.allSet(line_off, size)) {
	    row.markUsed(row_it);
	    return true;
	  } else {
	    return false;
	  }
	}
      }

      // next
      addr = tag;
      size = 1;
    }
    return false;
  }

  void setDeclassified(ADDRINT addr, unsigned size) {
    // If this range is already declassified, do nothing.
    if (checkDeclassified(addr, size, false))
      return;

    // This range is not declassified. This implies that we must create an
    // entry in the first table.
    for (unsigned table_idx = 0; table_idx < NumTables(); ++table_idx) {
      Table& table = tables[table_idx];
      const unsigned line_off = addr & (LineSize(table_idx) - 1);
      const ADDRINT tag = addr / LineSize(table_idx);
      const unsigned row_idx = tag & (TableRows(table_idx) - 1);
      Row& row = table[row_idx];

      // Try to find a matching line. 
      auto row_it = std::find_if(row.begin(), row.end(), [tag] (const Line& line) -> bool {
	return line.valid && line.tag == tag;
      });

      // If we didn't find a matching line, try to allocate a new one.
      if (row_it == row.end()) {
	row_it = row.allocateLine(stats_evictions[table_idx]);
	row_it->assign(tag, false);
      }
      assert(row_it != row.end());
      Line& line = *row_it;

      // Declassify entries.
      std::fill_n(line.bv.begin() + line_off, size, true);

      // If we are at the top-level table, then break.
      // Otherwise, we can potentially upgrade this line.
      if (table_idx == NumTables() - 1)
	break;
      
      if (!line.allSet())
	break;

      line.valid = false;
      ++stats_upgrades[table_idx];

      addr = tag;
      size = 1;
    }
  }

  void setClassified(ADDRINT addr, unsigned size, ADDRINT) {
    for (unsigned table_idx = 0; table_idx < NumTables(); ++table_idx) {
      Table& table = tables[table_idx];
      const unsigned line_off = addr & (LineSize(table_idx) - 1);
      const ADDRINT tag = addr / LineSize(table_idx);
      const unsigned row_idx = tag & (TableRows(table_idx) - 1);
      Row& row = table[row_idx];

      // Do we have a match?
      for (Line& line : row) {
	if (line.valid && line.tag == tag) {
	  std::fill_n(line.bv.begin() + line_off, size, false);
	  return;
	}
      }

      // Update "address" and "size".
      addr = tag;
      size = 1;
    }
  }

  bool setClassified2_Rec(ADDRINT addr, unsigned size, unsigned table_idx) {
    if (table_idx == NumTables())
      return false;
    
    Table& table = tables[table_idx];
    const unsigned line_off = addr & (LineSize(table_idx) - 1);
    const ADDRINT tag = addr / LineSize(table_idx);
    const unsigned row_idx = tag & (TableRows(table_idx) - 1);
    Row& row = table[row_idx];
    for (Line& line : row) {
      if (line.valid && line.tag == tag) {
	auto it = line.bv.begin() + line_off;
	auto it_end = it + size;
	const bool reclassified = *it; // NOTE: This breaks for the first level, but we discard that anyway.
	std::fill(it, it_end, false);
	if (line.allReset())
	  line.valid = false;
	return reclassified;
      }
    }

    if (setClassified2_Rec(tag, 1, table_idx + 1) && LineSize(table_idx) > size) {
      // Allocate a new line and set all bits except for ours.
      Line& line = *row.allocateLine(stats_evictions[table_idx]);
      line.assign(tag, true); // initialize to all true
      std::fill_n(line.bv.begin() + line_off, size, false); // clear out declassified bits
      assert(!line.allReset());
      ++stats_downgrades[table_idx];
    }
    
    return false;
  }

  void setClassified2(ADDRINT addr, unsigned size, ADDRINT) {
    setClassified2_Rec(addr, size, 0);
  }

  void clear() {
    for (auto& table : tables)
      for (auto& line : table)
	line.valid = false;
  }

  void printStats(std::ostream& os) {
    for (unsigned table_idx = 0; table_idx < NumTables(); ++table_idx) {
      os << "table" << table_idx << ".evictions " << stats_evictions[table_idx] << "\n";
      if (table_idx < NumTables() - 1) {
	os << "table" << table_idx << "->" << (table_idx + 1) << ".upgrades " << stats_upgrades[table_idx] << "\n";
	os << "table" << (table_idx + 1) << "->" << table_idx << ".downgrades " << stats_downgrades[table_idx] << "\n";
      }
    }
  }

  void printDesc(std::ostream& os) {
    os << "Configuration:\n"
       << "\ttable\tline_size\ttable_size\tassociativity\n";
    for (unsigned table_idx = 0; table_idx < NumTables(); ++table_idx) {
      os << "\t" << table_idx << "\t" << LineSize(table_idx) << "\t" << TableSize(table_idx)
	 << "\t" << Associativity(table_idx) << "\n";
    }
  }

private:
  std::vector<Table> tables;
  std::array<unsigned long, NumTables()-1> stats_upgrades;
  std::array<unsigned long, NumTables()> stats_evictions;
  std::array<unsigned long, NumTables()-1> stats_downgrades;
};

