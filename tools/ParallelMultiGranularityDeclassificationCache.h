#pragma once

#define LOG_EVICTIONS 0

template <unsigned LineSize_, unsigned Associativity_, unsigned TableSize_, unsigned Scale_>
class RealDeclassificationCache {
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
    Line& getOrAllocateLine(ADDRINT tag, bool& evicted, std::array<bool, LineSize>& evicted_bv) {
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
      const auto metric = [&] (const Line& line) -> int {
	return used[&line - lines.data()];
      };
      auto evicted_it = std::min_element(lines.begin(), lines.end(), [&] (const Line& a, const Line& b) -> bool {
	return metric(a) < metric(b);
      });
      assert(evicted_it != lines.end());

      evicted = true;
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

  void setDeclassified(ADDRINT addr, unsigned size) {
    const unsigned line_off = addr & (LineSize - 1);
    const ADDRINT tag = addr / LineSize;
    const unsigned row_idx = tag & (TableRows - 1);
    Row& row = rows[row_idx];
    
    bool evicted;
    std::array<bool, LineSize> evicted_bv;
    Line& line = row.getOrAllocateLine(tag, evicted, evicted_bv);
    line.set(line_off, size);
    row.markUsed(&line);

    if (evicted) {
      constexpr int eviction_sample_rate = 1024;
      ++stat_evictions;
      if (stat_evictions % eviction_sample_rate == 0) {
	std::array<uint8_t, LineSize / 8> buf;
	for (unsigned i = 0; i < LineSize; i += 8) {
	  uint8_t& value = buf[i / 8] = 0;
	  for (unsigned j = 0; j < 8; ++j) {
	    value <<= 1;
	    if (evicted_bv[i + j])
	      value |= 1;
	  }
	}
#if LOG_EVICTIONS
	fwrite(buf.data(), 1, buf.size(), eviction_file);
#endif
      }
    }
  }

  void setDeclassifiedNoAllocate(ADDRINT addr, unsigned size) {
    const unsigned line_off = addr & (LineSize - 1);
    const ADDRINT tag = addr / LineSize;
    const unsigned row_idx = tag & (TableRows - 1);
    Row& row = rows[row_idx];
    Line *line = row.tryGetLine(tag);
    if (!line)
      return;
    line->set(line_off, size);
  }

  void setClassified(ADDRINT addr, unsigned size) {
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
    os << "declassifiaction cache: linesize=" << LineSize << " associativity=" << Associativity << " numlines=" << TableSize << "\n";
  }

  void printStats(std::ostream& os) {
    os << "evictions-cache-" << Scale_ << " " << stat_evictions << "\n";
  }

  RealDeclassificationCache() {
#if LOG_EVICTIONS
    char path[256];
    sprintf(path, "evictions%u.bin", Scale_);
    eviction_file = fopen(path, "w");
#endif
  }

private:
  std::array<Row, TableRows> rows;
  unsigned long stat_evictions = 0;
  FILE *eviction_file;
};


template <std::array<unsigned, 4> LineSize, std::array<unsigned, 4> Associativity, std::array<unsigned, 4> TableSize>
class RealDeclassificationCaches {
public:
  bool checkDeclassified(ADDRINT addr, unsigned size) {
    switch (size) {
    case 1:
      return cache1.checkDeclassified(addr / 1, div_up(size, 1U));
    case 2:
      return cache2.checkDeclassified(addr / 2, div_up(size, 2U));
    case 4:
      return cache4.checkDeclassified(addr / 4, div_up(size, 4U));
    case 8:
    case 16:
    case 32:
    case 64:
      return cache8.checkDeclassified(addr / 8, div_up(size, 8U));
    default:
      return false;
    }
  }

  void setDeclassified(ADDRINT addr, unsigned size) {
    assert(size <= 64);
    switch (size) {
    case 1:
      cache1.setDeclassified(addr / 1, 1);
      break;
    case 2:
      cache2.setDeclassified(addr / 2, 1);
      break;
    case 4:
      cache4.setDeclassified(addr / 4, 1);
      break;
    case 8:
    case 16:
    case 32: 
    case 64:
      cache8.setDeclassified(addr / 8, size / 8);
      break;
    default:
      break;
    }
  }

  void setClassified(ADDRINT addr, unsigned size, ADDRINT) {
    cache1.setClassified(addr, size);
    cache2.setClassified(addr / 2, div_up<unsigned>(size, 2));
    cache4.setClassified(addr / 4, div_up<unsigned>(size, 4));
    cache8.setClassified(addr / 8, div_up<unsigned>(size, 8));
  }

  void printStats(std::ostream& os) {
    cache1.printStats(os);
    cache2.printStats(os);
    cache4.printStats(os);
    cache8.printStats(os);
  }

  void printDesc(std::ostream& os) {
    cache1.printDesc(os);
    cache2.printDesc(os);
    cache4.printDesc(os);
    cache8.printDesc(os);
  }

  void dumpTaint(std::vector<uint8_t>&) {}
  
private:
  RealDeclassificationCache<LineSize[0], Associativity[0], TableSize[0], 1> cache1;
  RealDeclassificationCache<LineSize[1], Associativity[1], TableSize[1], 2> cache2;
  RealDeclassificationCache<LineSize[2], Associativity[2], TableSize[2], 4> cache4;
  RealDeclassificationCache<LineSize[3], Associativity[3], TableSize[3], 8> cache8;
};
