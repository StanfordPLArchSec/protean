#include "pin.H"

#include <array>

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
