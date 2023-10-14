#pragma once

#include "pin.H"
#include <array>
#include <string>

template <unsigned LineSize_, unsigned Associativity_, unsigned TableSize_>
class DeclassificationCache {
public:
  static inline constexpr unsigned LineSize = LineSize_;
  static inline constexpr unsigned Associativity = Associativity_;
  static inline constexpr unsigned TableSize = TableSize_;
  static inline constexpr unsigned TableCols = Associativity;
  static inline constexpr unsigned TableRows = TableSize / TableCols;
  static_assert((LineSize & (LineSize - 1)) == 0, "");
  static_assert((TableRows & (TableRows - 1)) == 0, "");

  struct Line {
    bool valid;
    ADDRINT tag;
    std::array<bool, LineSize> bv;

    Line(): valid(false) {}

    bool allSet(unsigned first, unsigned size) const {
      const auto it = bv.begin() + first;
      return std::reduce(it, it + size, true, std::logical_and<bool>());
    }

    bool anySet() const {
      return std::reduce(bv.begin(), bv.end(), false, std::logical_or<bool>());
    }

    bool allReset() const {
      return !anySet();
    }

    // Returns whether it was updated
    bool set(unsigned first, unsigned size) {
      bool updated = false;
      for (unsigned i = 0; i < size; ++i) {
	if (!bv[first + i]) {
	  updated = true;
	  bv[first + i] = true;
	}
      }
      return updated;
    }

    bool reset(unsigned first, unsigned size) {
      bool updated = true;
      for (unsigned i = 0; i < size; ++i) {
	if (bv[first + i]) {
	  updated = true;
	  bv[first + i] = false;
	}
      }
      return updated;
    }
  };

  class Row {
  public:
    using Tick = unsigned;
    using Lines = std::array<Line, TableCols>;

    // Stores the evicted line, if any, in @evicted. If no line
    // was evicted, then evicted.valid == false.
    Line& getOrAllocateLine(ADDRINT tag, bool& evicted, ADDRINT& evicted_tag, std::array<bool, LineSize>& evicted_bv) {
      evicted = false;
      
      // First, check for matching line.
      for (Line& line : lines)
	if (line.valid && line.tag == tag)
	  return line;

      const auto init_line = [&] (Line& line) {
	line.valid = true;
	line.tag = tag;
	std::fill(line.bv.begin(), line.bv.end(), false);
      };

      // Check for free line.
      for (Line& line : lines) {
	if (!line.valid) {
	  init_line(line);
	  return line;
	}
      } 
      
      // Check for line with lowest population count.
      const auto metric_used = [&] (const Line& line) -> int {
	return used[&line - lines.data()];
      };
      const auto metric_popcnt = [] (const Line& line) -> int {
	return std::count(line.bv.begin(), line.bv.end(), true);
      };
      const auto metric = [&] (const Line& line) -> float {
	return metric_used(line) + metric_popcnt(line);
      };
      auto evicted_it = std::min_element(lines.begin(), lines.end(), [&] (const Line& a, const Line& b) -> bool {
	return metric(a) < metric(b);
      });
      assert(evicted_it != lines.end());

      evicted = true;
      evicted_tag = evicted_it->tag;
      std::copy(evicted_it->bv.begin(), evicted_it->bv.end(), evicted_bv.begin());

      init_line(*evicted_it);
      return *evicted_it;
    }

    Line *tryGetLine(ADDRINT tag) {
      for (typename Lines::iterator it = lines.begin(); it != lines.end(); ++it) {
	if (it->valid && it->tag == tag) {
	  return &*it;
	}
      }
      return nullptr;
    }

    void markUsed(Line *line) {
      const unsigned row_idx = line - lines.data();
      used[row_idx] = ++tick;
    }

  private:
    Lines lines;
    Tick tick = 0;
    std::array<Tick, TableCols> used;
  };


  bool checkDeclassified(ADDRINT addr, unsigned size) {
    const unsigned line_off = addr & (LineSize - 1);
    const ADDRINT tag = addr / LineSize;
    const unsigned row_idx = tag & (TableRows - 1);
    Row& row = rows[row_idx];

    Line *line = row.tryGetLine(tag);
    if (line == nullptr)
      return false;

    row.markUsed(line);

    assert(line->valid);
    return line->allSet(line_off, size);
  }

  void setDeclassified(ADDRINT addr, unsigned size, bool& evicted, ADDRINT& evicted_addr, std::array<bool, LineSize>& evicted_bv) {
    evicted = false;
    evicted_addr = 0;
    
    const unsigned line_off = addr & (LineSize - 1);
    const ADDRINT tag = addr / LineSize;
    const unsigned row_idx = tag & (TableRows - 1);
    Row& row = rows[row_idx];

    ADDRINT evicted_tag;
    Line& line = row.getOrAllocateLine(tag, evicted, evicted_tag, evicted_bv);
    line.set(line_off, size);
    row.markUsed(&line);

    if (evicted) {
      evicted_addr = evicted_tag * LineSize;
    }
  }

  void setDeclassified(ADDRINT addr, unsigned size) {
    bool evicted;
    ADDRINT evicted_addr;
    std::array<bool, LineSize> evicted_bv;
    setDeclassified(addr, size, evicted, evicted_addr, evicted_bv);
  }

  void setClassified(ADDRINT addr, unsigned size, ADDRINT store_inst) {
    const unsigned line_off = addr & (LineSize - 1);
    const ADDRINT tag = addr / LineSize;
    const unsigned row_idx = tag & (TableRows - 1);
    Row& row = rows[row_idx];
    Line *line = row.tryGetLine(tag);
    if (!line)
      return;
    line->reset(line_off, size);
    if (line->allReset())
      line->valid = false;
  }

  void printDesc(std::ostream& os) {
    os << name << ": declassification cache with parameters linesize=" << LineSize << " associativity=" << Associativity << " numlines=" << TableSize << "\n";
  }

  void printStats(std::ostream& os) {
    os << name << ".evictions " << stat_evictions << "\n";
  }

  DeclassificationCache(const std::string& name): name(name) {}

private:
  std::string name;
  std::array<Row, TableRows> rows;
  unsigned long stat_evictions = 0;
  FILE *eviction_file;
};
