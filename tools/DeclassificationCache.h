#pragma once

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
    using Lines = std::array<Line, TableCols>;

    // Stores the evicted line, if any, in @evicted. If no line
    // was evicted, then evicted.valid == false.
    Line& getOrAllocateLine(ADDRINT tag) {
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
      auto evicted_it = std::min_element(lines.begin(), lines.end(), [] (const Line& a, const Line& b) -> bool {
	const auto popcnt = [] (const Line& line) -> unsigned {
	  return std::count(line.bv.begin(), line.bv.end(), true);
	};
	return popcnt(a) < popcnt(b);
      });
      assert(evicted_it != lines.end());
      evicted_it = lines.begin() + std::rand() % lines.size();

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
    

  private:
    Lines lines;
  };


  bool checkDeclassified(ADDRINT addr, unsigned size) {
    const unsigned line_off = addr & (LineSize - 1);
    const ADDRINT tag = addr / LineSize;
    const unsigned row_idx = tag & (TableRows - 1);
    Row& row = rows[row_idx];

    Line *line = row.tryGetLine(tag);
    if (line == nullptr)
      return false;

    assert(line->valid);
    return line->allSet(line_off, size);
  }

  void setDeclassified(ADDRINT addr, unsigned size) {
    const unsigned line_off = addr & (LineSize - 1);
    const ADDRINT tag = addr / LineSize;
    const unsigned row_idx = tag & (TableRows - 1);
    Row& row = rows[row_idx];
    Line& line = row.getOrAllocateLine(tag);
    line.set(line_off, size);
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
  }

private:
  std::array<Row, TableRows> rows;
};


template <std::array<unsigned, 3> LineSize, std::array<unsigned, 3> Associativity, std::array<unsigned, 3> TableSize>
class RealDeclassificationCaches {
public:
  bool checkDeclassified(ADDRINT addr, unsigned size) {
    return cache1.checkDeclassified(addr, size)
      || cache4.checkDeclassified(addr / 4, div_up<unsigned>(size, 4))
      || cache8.checkDeclassified(addr / 8, div_up<unsigned>(size, 8));
  }

  void setDeclassified(ADDRINT addr, unsigned size) {
    switch (size) {
    case 1:
      cache1.setDeclassified(addr, 1);
      break;
    case 2:
      break;
    case 4:
      cache4.setDeclassified(addr / 4, 1);
      break;
    case 8:
      cache8.setDeclassified(addr / 8, 1);
      break;
    case 16:
      cache8.setDeclassified(addr / 8, 2);
      break;
    default: abort();
    }
  }

  void setClassified(ADDRINT addr, unsigned size, ADDRINT) {
    cache1.setClassified(addr, size);
    cache4.setClassified(addr / 4, div_up<unsigned>(size, 4));
    cache8.setClassified(addr / 8, div_up<unsigned>(size, 8));
  }

  void printStats(std::ostream& os) {
    cache1.printStats(os);
    cache4.printStats(os);
    cache8.printStats(os);
  }
  void printDesc(std::ostream& os) {
    cache1.printDesc(os);
    cache4.printDesc(os);
    cache8.printDesc(os);
  }
  
private:
  RealDeclassificationCache<LineSize[0], Associativity[0], TableSize[0], 1> cache1;
  RealDeclassificationCache<LineSize[1], Associativity[1], TableSize[1], 4> cache4;
  RealDeclassificationCache<LineSize[2], Associativity[2], TableSize[2], 8> cache8;
};
