#pragma once

#include "pin.H"
#include <array>
#include <string>

#include "Util.h"

template <unsigned LineSize_, unsigned Associativity_, unsigned TableSize_, class EvictionPolicy_>
class DeclassificationCache {
public:
  static inline constexpr unsigned LineSize = LineSize_;
  static inline constexpr unsigned Associativity = Associativity_;
  static inline constexpr unsigned TableSize = TableSize_;
  static inline constexpr unsigned TableCols = Associativity;
  static inline constexpr unsigned TableRows = TableSize / TableCols;
  static_assert((LineSize & (LineSize - 1)) == 0, "");
  static_assert((TableRows & (TableRows - 1)) == 0, "");
  using EvictionPolicy = EvictionPolicy_;

  struct Line {
    bool valid;
    ADDRINT tag;
    std::array<bool, LineSize> bv;
    unsigned long stat_hits = 0;

    Line(): valid(false) {}

    bool contains(Addr addr) const {
      return valid && tag == addr / LineSize;
    }

    Addr baseaddr() const { return tag * LineSize; }

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

    void dump(std::ostream& os) const {
      if (valid) {
	char tag_s[256];
	sprintf(tag_s, "%016lx", tag);
	os << tag_s << " " << bv_to_string8(bv.begin(), bv.end()) << " " << stat_hits;
      } else {
	os << "xxxxxxxxxxxxxxxx xxxxxxxxxxxxxxxx";
      }
    }
  };

  class Row {
  public:
    using Tick = unsigned;
    using Lines = std::array<Line, TableCols>;

    Row() = default;
    Row(const EvictionPolicy& eviction_policy): eviction_policy(eviction_policy) {}

    bool contains(Addr addr) const {
      return std::any_of(lines.begin(), lines.end(), [addr] (const Line& line) {
	return line.contains(addr);
      });
    }

    // Stores the evicted line, if any, in @evicted. If no line
    // was evicted, then evicted.valid == false.
    Line& getOrAllocateLine(ADDRINT tag, bool& evicted, Line& evicted_line) {
      evicted = false;
      
      // First, check for matching line.
      for (Line& line : lines)
	if (line.valid && line.tag == tag)
	  return line;

      const auto init_line = [&] (Line& line) {
	line.valid = true;
	line.stat_hits = 0;
	line.tag = tag;
	std::fill(line.bv.begin(), line.bv.end(), false);
	eviction_policy.allocated(getIndex(line), line.bv);
      };

      // Check for free line.
      for (Line& line : lines) {
	if (!line.valid) {
	  init_line(line);
	  return line;
	}
      }

      // Check for line with lowest population count.
      const auto metric = [&] (const Line& line) -> int {
	return eviction_policy.score(&line - lines.data(), line.bv);
      };
      auto evicted_it = std::min_element(lines.begin(), lines.end(), [&] (const Line& a, const Line& b) -> bool {
	return metric(a) < metric(b);
      });
      assert(evicted_it != lines.end());

      evicted = true;
      evicted_line = *evicted_it;

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

    unsigned getIndex(const Line *line) const {
      return line - lines.data();
    }

    unsigned getIndex(const Line& line) const {
      return getIndex(&line);
    }

    void dump(std::ostream& os, const std::string& prefix) const {
      for (const Line& line : lines) {
	os << prefix << " ";
	line.dump(os);
	os << "\n";
      }
    }

  private:
    Lines lines;
  public:
    EvictionPolicy eviction_policy;
  };

  bool contains(Addr addr) const {
    const ADDRINT tag = addr / LineSize;
    const unsigned row_idx = tag & (TableRows - 1);
    const Row& row = rows[row_idx];
    return row.contains(addr);
  }

  bool checkDeclassified(ADDRINT addr, unsigned size) {
    const unsigned line_off = addr & (LineSize - 1);
    const ADDRINT tag = addr / LineSize;
    const unsigned row_idx = tag & (TableRows - 1);
    Row& row = rows[row_idx];

    Line *line = row.tryGetLine(tag);
    if (line == nullptr)
      return false;

    assert(line->valid);
    const bool is_declassified = line->allSet(line_off, size);
    if (is_declassified) {
      row.eviction_policy.checkDeclassifiedHit(row.getIndex(line), line->bv);
      ++line->stat_hits;
    } else {
      row.eviction_policy.checkDeclassifiedMiss(row.getIndex(line), line->bv);
    }
    
    return is_declassified;
  }

  void setDeclassified(ADDRINT addr, unsigned size, bool& evicted,
		       ADDRINT& evicted_addr, std::array<bool, LineSize>& evicted_bv) {
    evicted = false;
    evicted_addr = 0;
    
    const unsigned line_off = addr & (LineSize - 1);
    const ADDRINT tag = addr / LineSize;
    const unsigned row_idx = tag & (TableRows - 1);
    Row& row = rows[row_idx];

    Line evicted_line;
    Line& line = row.getOrAllocateLine(tag, evicted, evicted_line);

    row.eviction_policy.setDeclassifiedPre(row.getIndex(line), line.bv);

    line.set(line_off, size);

    row.eviction_policy.setDeclassifiedPost(row.getIndex(line), line.bv);    

    if (evicted) {
      evicted_addr = evicted_line.tag * LineSize;
      std::copy(evicted_line.bv.begin(), evicted_line.bv.end(), evicted_bv.begin());
      ++stat_evictions;
      stat_evicted_bits += popcnt(evicted_line.bv);
      if (evicted_line.stat_hits == 0)
	++stat_evictions_unused;
      stat_evictions_hits += evicted_line.stat_hits;
      static unsigned long iter = 0;
      ++iter;
      constexpr unsigned freq = 1000;
      if (iter % freq == 0 && eviction_file) {
	fprintf(eviction_file, "%016lx %s %lu\n",
		evicted_line.baseaddr(), bv_to_string8(evicted_line.bv).c_str(), evicted_line.stat_hits);
      }
    }
  }

  void setDeclassified(ADDRINT addr, unsigned size, Addr inst) {
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
    
    row.eviction_policy.setClassifiedPre(row.getIndex(line), line->bv);

    line->reset(line_off, size);

    row.eviction_policy.setClassifiedPost(row.getIndex(line), line->bv);

    if (line->allReset())
      line->valid = false;
  }



  void downgradeLine(Addr addr, std::array<bool, LineSize>& bv, bool& evicted, Addr& evicted_addr, std::array<bool, LineSize>& evicted_bv) {
    assert((addr & (LineSize - 1)) == 0);
    const Addr tag = addr / LineSize;
    const unsigned row_idx = tag & (TableRows - 1);
    Row& row = rows[row_idx];
    Line evicted_line;
    Line& line = row.getOrAllocateLine(tag, evicted, evicted_line);
    evicted_addr = evicted_line.tag * LineSize;
    evicted_bv = evicted_line.bv;
    line.bv = bv;
  }

  void claimLine(Addr addr, const std::array<bool, LineSize>& bv) {
    fprintf(stderr, "evictions %zu\n", stat_evictions);
    
    // TODO: Merge w/ logic in setDeclassified.
    assert((addr & (LineSize - 1)) == 0);
    assert(!contains(addr));
    const Addr tag = addr / LineSize;
    const unsigned row_idx = tag & (TableRows - 1);
    Row& row = rows[row_idx];
    bool evicted;
    Line evicted_line;
    Line& line = row.getOrAllocateLine(tag, evicted, evicted_line);
    line.bv = bv;
    if (evicted) {
      ++stat_evictions;
      stat_evicted_bits += popcnt(evicted_line.bv);
      if (evicted_line.stat_hits == 0)
	++stat_evictions_unused;
      stat_evictions_hits += evicted_line.stat_hits;
    }
  }

  void printDesc(std::ostream& os) const {
    os << name << ": declassification cache with parameters linesize=" << LineSize << " associativity=" << Associativity << " numlines=" << TableSize << "\n";
  }

  void printStats(std::ostream& os) const {
    os << name << ".evictions " << stat_evictions << "\n";
    os << name << ".evicted_bits " << stat_evicted_bits << "\n";
    os << name << ".evictions_unused " << stat_evictions_unused << "\n";
    os << name << ".evictions_hits " << stat_evictions_hits << "\n";
  }

  void dump(std::ostream& os) const {
    for (size_t i = 0; i < rows.size(); ++i) {
      rows[i].dump(os, std::to_string(i));
    }
  }

  void dumpTaint(auto&) {}

  DeclassificationCache(const std::string& name, const EvictionPolicy& eviction_policy, FILE *& eviction_file, unsigned eviction_dump_freq): name(name), eviction_file(eviction_file), eviction_dump_freq(1000) {
    std::fill(rows.begin(), rows.end(), Row(eviction_policy));
  }

private:
  std::string name;
  std::array<Row, TableRows> rows;
  unsigned long stat_evictions = 0;
  unsigned long stat_evicted_bits = 0;
  unsigned long stat_evictions_unused = 0;
  unsigned long stat_evictions_hits = 0;
  FILE * & eviction_file;
  unsigned eviction_dump_freq;
};
