#pragma once

#include <algorithm>
#include <array>

#include "Util.h"
#include "ShadowMemory.h"

template <unsigned LineSize_, unsigned MaxPatternLength_>
class IdealPatternDeclassificationTable {
public:
  static inline constexpr unsigned LineSize = LineSize_;
  static_assert((LineSize & (LineSize - 1)) == 0, "");
  static inline constexpr unsigned MaxPatternLength = MaxPatternLength_;
  static_assert(MaxPatternLength <= LineSize, "");
  
  IdealPatternDeclassificationTable(): shadow(INVALID) {}

  enum Type {
    INVALID,
    DECLASSIFIED,
    CLASSIFIED,
  };

private:
  bool checkAllDeclassified(Type *first, Type *last) const {
    return std::all_of(first, last, [] (Type type) -> bool {
      return type == DECLASSIFIED;
    });
  }

  bool checkAnyDeclassified(Type *first, Type *last) const {
    return std::any_of(first, last, [] (Type type) -> bool {
      return type == DECLASSIFIED;
    });
  }

  void invalidateLine(Addr addr) {
    const Addr base = addr & ~static_cast<Addr>(LineSize - 1);
    Type *base_ptr = &shadow[base];
    std::fill_n(base_ptr, LineSize, INVALID);
  }

public:
  bool checkDeclassified(Addr addr, unsigned size) {
    Type *ptr = &shadow[addr];
    return checkAllDeclassified(ptr, ptr + size);
  }

  bool setDeclassified(Addr addr, unsigned size,
		       bool& downgrade, Addr& downgrade_addr, std::array<bool, LineSize>& downgrade_bv
		       ) {
    downgrade = false;
    
    Type *ptr = &shadow[addr];
    if (checkAllDeclassified(ptr, ptr + size))
      return true;
#if 1
    invalidateLine(addr);
    return false;
#elif 0
    Type *baseptr = &shadow[addr & ~static_cast<Addr>(LineSize - 1)];
    if (std::count(baseptr, baseptr + LineSize, INVALID) == 0) {
      std::fill_n(ptr, size, DECLASSIFIED);
      return true;
    }
    return false;
#else
    
    downgrade_addr = addr & ~static_cast<Addr>(LineSize - 1);
    Type *baseptr = &shadow[downgrade_addr];
    const auto invalid_count = std::count(baseptr, baseptr + LineSize, INVALID);
    assert(invalid_count == 0 || invalid_count == LineSize);
    if (invalid_count == LineSize)
      return false;
    downgrade = true;
    std::transform(baseptr, baseptr + LineSize, downgrade_bv.begin(),
		   [] (Type type) -> bool {
		     assert(type != INVALID);
		     return type == DECLASSIFIED;
		   });
    invalidateLine(addr);
    return false;
#endif
  }

  void setClassified(Addr addr, unsigned size, Addr store_inst,
		     bool& downgrade, Addr& downgrade_addr, std::array<bool, LineSize>& downgrade_bv
		     ) {
    downgrade = false;
    
    Type *ptr = &shadow[addr];
    if (!checkAnyDeclassified(ptr, ptr + size))
      return;

#if 1
    invalidateLine(addr);
#elif 0
    Type *baseptr = &shadow[addr & ~static_cast<Addr>(LineSize - 1)];
    if (std::count(baseptr, baseptr + LineSize, INVALID) == 0) {
      std::fill_n(ptr, size, CLASSIFIED);
    }
#else
    downgrade_addr = addr & ~static_cast<Addr>(LineSize - 1);
    Type *baseptr = &shadow[downgrade_addr];
    const auto invalid_count = std::count(baseptr, baseptr + LineSize, INVALID);
    assert(invalid_count == 0 || invalid_count == LineSize);
    if (invalid_count == LineSize)
      return;
    
    downgrade = true;
    std::transform(baseptr, baseptr + LineSize, downgrade_bv.begin(),
		   [] (Type type) -> bool {
		     assert(type != INVALID);
		     return type == DECLASSIFIED;
		   });
    invalidateLine(addr);
#endif
  }

private:
  bool hasPattern(const std::array<bool, LineSize>& bv, std::vector<bool>& pattern) {
    const unsigned max_pattern_length = std::min<unsigned>(MaxPatternLength, bv.size());
    for (unsigned i = 1; i <= max_pattern_length; ++i) {
      bool matched = true;
      for (unsigned j = i; j < bv.size(); j += i) {
	const unsigned len = std::min<unsigned>(i, bv.size() - j);
	if (!std::equal(bv.begin(), bv.begin() + len,
			bv.begin() + j, bv.begin() + j + len)) {
	  matched = false;
	  break;
	}
      }
      if (matched) {
	std::copy(bv.begin(), bv.begin() + i, std::back_inserter(pattern));
	return true;
      }
    }
    return false;
  }

public:
  bool claimLine(Addr addr, const std::array<bool, LineSize>& bv) {
    // Check if we can identify a pattern.
    std::vector<bool> pattern;
    if (hasPattern(bv, pattern)) {
#if 0
      fprintf(stderr, "PATTERN: %s -> %s\n",
	      bv_to_string8(bv.begin(), bv.end()).c_str(),
	      bv_to_string1(pattern.begin(), pattern.end()).c_str());
#endif
      assert((addr & (LineSize - 1)) == 0);
      Type *ptr = &shadow[addr];
      std::transform(bv.begin(), bv.end(), ptr, [] (bool taint) -> Type {
	return taint ? DECLASSIFIED : CLASSIFIED;
      });
      ++stat_match;
      return true;
    }
    ++stat_nomatch;
    return false;
  }

  void printDesc(std::ostream& os) {
    os << "ideal pattern declassification table backed by shadow memory\n";
  }

  void printStats(std::ostream& os) {
    os << "pattern-match " << stat_match << "\n"
       << "pattern-nomatch " << stat_nomatch << "\n";
  }

  void dump(std::ostream& os) {} 

private:
  ShadowMemory<Type , 0, 12> shadow;
  unsigned long stat_match = 0;
  unsigned long stat_nomatch = 0;
};






template <unsigned ChunkSize_, unsigned LineSize_, unsigned Associativity_, unsigned TableSize_, unsigned MaxPatLen_>
class PatternDeclassificationCache {
public:
  static inline constexpr unsigned ChunkSize = ChunkSize_;
  static inline constexpr unsigned LineSize = LineSize_;
  static_assert((LineSize & (LineSize - 1)) == 0, "");
  static inline constexpr unsigned Associativity = Associativity_;
  static inline constexpr unsigned TableCols = Associativity;
  static inline constexpr unsigned TableSize = TableSize_;
  static inline constexpr unsigned TableRows = TableSize / TableCols;
  static_assert(TableSize % TableCols == 0, "");
  static_assert((TableRows & (TableRows - 1)) == 0, "");
  static inline constexpr unsigned MaxPatLen = MaxPatLen_;
  static_assert(MaxPatLen <= LineSize, "");

  using PatBV = std::array<bool, ChunkSize * 2>;
  using DataBV = std::array<bool, LineSize>;

  class Line {
  public:
    Line(): valid_(false) {}
    
    template <typename InputIt>
    Line(Addr lower_tag, InputIt pat_first, InputIt pat_last): valid_(true), tag_(lower_tag / LineSize) {
      // Need to align pattern with beginning of line properly.
      // But first, we need to know the pattern length.
      patlen_ = pat_last - pat_first;

      // Copy and align first iteration of pattern into `pat_`.
      const Addr lower_baseaddr = lower_tag * ChunkSize;
      const Addr baseaddr = tag_ * LineSize * ChunkSize;
      const unsigned rshift = (lower_baseaddr - baseaddr) % patlen_;
      std::copy_n(pat_first, patlen_ - rshift,  pat_.begin() + rshift);
      std::copy_n(pat_first + patlen_ - rshift, rshift, pat_.begin());

      // Expand pattern repetitions into `pat_`.
      for (unsigned i = patlen_; i < pat_.size(); i += patlen_) {
	std::copy_n(pat_.begin(), std::min<unsigned>(patlen_, pat_.size() - i), pat_.begin() + i);
      }

      // Initialize data bitvector.
      // TODO: Might want to move this elsewhere, or call a separate function for performing the check?
      std::fill(bv_.begin(), bv_.end(), false);
      const unsigned line_idx = lower_tag & (LineSize - 1);
      bv_[line_idx] = true;
    }

    bool valid() const { return valid_; }
    void invalidate() {
      assert(valid());
      valid_ = false;
    }

    bool allSet() const {
      assert(valid());
      return std::reduce(bv_.begin(), bv_.end(), true, std::logical_and<bool>());
    }

    template <typename OutputIt>
    OutputIt getPattern(OutputIt out) const {
      return std::copy_n(pat_.begin(), patlen_, out);
    }

    Addr getBaseAddr() const {
      return tag_ * LineSize * ChunkSize;
    }

    // Returns whether the match was successful.
    template <typename InputIt>
    bool tryMatchAndSet(Addr lower_tag, InputIt bv_first, InputIt bv_last) {
      assert(bv_last - bv_first == ChunkSize);
      assert(contains(lower_tag * ChunkSize));

      // Compute the pattern shift.
      const Addr lower_baseaddr = lower_tag * ChunkSize;
      const Addr baseaddr = tag_ * LineSize * ChunkSize;
      const unsigned lshift = (lower_baseaddr - baseaddr) % patlen_;

      // We can do this since we previously expanded the pattern.
      if (!std::equal(bv_first, bv_last, pat_.begin() + lshift)) {
	return false;
      }

      const unsigned line_idx = lower_tag & (LineSize - 1);
      bv_[line_idx] = true;
      return true;
    }

    bool contains(Addr addr) const {
      if (!valid_)
	return false;

      const Addr tag = addr / ChunkSize / LineSize;
      if (tag != tag_)
	return false;

      return true;
    }

  private:
    bool check(Addr addr, unsigned size, bool check) {
      assert(contains(addr));

      const unsigned line_idx = (addr / ChunkSize) & (LineSize - 1);
      if (!bv_[line_idx])
	return false;

      // Compute pattern offset.
      const Addr baseaddr = tag_ * LineSize * ChunkSize;
      const unsigned lshift = (addr - baseaddr) % patlen_;
      if (!std::all_of(pat_.begin() + lshift, pat_.begin() + lshift + size,
		       [check] (bool b) {
			 return b == check;
		       }))
	return false;
		  
      return true;
    }

    bool set(Addr addr, unsigned size, bool value) {
      if (check(addr, size, value))
	return true;

      // Invalidate chunk for now.
      const unsigned line_idx = (addr / ChunkSize) & (LineSize - 1);
      bv_[line_idx] = false;
      if (std::count(bv_.begin(), bv_.end(), true) == 0)
	valid_ = false;
      return false;      
    }

  public:
    bool checkDeclassified(Addr addr, unsigned size) {
      return check(addr, size, true);
    }

    bool setDeclassified(Addr addr, unsigned size) {
      return set(addr, size, true);
    }

    bool setClassified(Addr addr, unsigned size) {
      return set(addr, size, false);
    }

    void dump(std::ostream& os) const {
      char buf[256];
      if (valid()) {
      sprintf(buf, "addr=%016lx patlen=%u pattern=%s data=%s",
	      tag_ * LineSize * ChunkSize,
	      patlen_,
	      bv_to_string1(pat_.begin(), pat_.begin() + patlen_).c_str(),
	      bv_to_string8(bv_.begin(), bv_.end()).c_str()
	      );
      } else {
	sprintf(buf, "(invalid)");
      }
      os << buf;
      
    }

  private:
    bool valid_;
    Addr tag_;
    PatBV pat_;
    unsigned patlen_;
    DataBV bv_;
  };


  class Row {
  public:

    bool checkDeclassified(Addr addr, unsigned size) {
      for (Line& line : lines)
	if (line.contains(addr))
	  return line.checkDeclassified(addr, size);
      return false;
    }

    bool setDeclassified(Addr addr, unsigned size) {
      for (Line& line : lines)
	if (line.contains(addr))
	  return line.setDeclassified(addr, size);
      return false;
    }

    bool setClassified(Addr addr, unsigned size) {
      for (Line& line : lines)
	if (line.contains(addr))
	  return line.setClassified(addr, size);
      return false;
    }

    Line *tryGetLine(Addr lower_tag) {
      for (Line& line : lines) {
	if (line.contains(lower_tag * ChunkSize)) {
	  return &line;
	}
      }
      return nullptr;
    }

    template <typename InputIt>
    Line& allocateLine(Addr lower_tag, InputIt pat_first, InputIt pat_last, unsigned long& stat_evictions) {
      assert(tryGetLine(lower_tag) == nullptr);
      
      // Try to find invalid/empty line.
      for (Line& line : lines) {
	if (!line.valid()) {
	  line = Line(lower_tag, pat_first, pat_last);
	  assert(line.contains(lower_tag * ChunkSize));
	  return line;
	}
      }
      
      // Fall back to random eviction policy for now.
      const unsigned row_idx = std::rand() % lines.size();
      Line& line = lines[row_idx];
      line = Line(lower_tag, pat_first, pat_last);
      ++stat_evictions;
      return line;
    }

    void dump(std::ostream& os) const {
      for (const Line& line : lines) {
	line.dump(os);
	os << "\n";
      }
    }
    
  private:
    std::array<Line, TableCols> lines;
  };


  bool checkDeclassified(Addr addr, unsigned size) {
    const unsigned idx = (addr / ChunkSize / LineSize) & (TableRows - 1);
    Row& row = rows[idx];
    return row.checkDeclassified(addr, size);
  }

  bool setDeclassified(Addr addr, unsigned size) {
    const unsigned idx = (addr / ChunkSize / LineSize) & (TableRows - 1);
    Row& row = rows[idx];
    return row.setDeclassified(addr, size);
  }

  bool setClassified(Addr addr, unsigned size, Addr store_inst) {
    const unsigned idx = (addr / ChunkSize / LineSize) & (TableRows - 1);
    Row& row = rows[idx];
    return row.setClassified(addr, size);
  }

private:

  bool hasPattern(const std::array<bool, ChunkSize>& bv, std::vector<bool>& pattern) {
    const unsigned max_pattern_length = std::min<unsigned>(16, bv.size());
    for (unsigned i = 1; i <= max_pattern_length; ++i) {
      bool matched = true;
      for (unsigned j = i; j < bv.size(); j += i) {
	const unsigned len = std::min<unsigned>(i, bv.size() - j);
	if (!std::equal(bv.begin(), bv.begin() + len,
			bv.begin() + j, bv.begin() + j + len)) {
	  matched = false;
	  break;
	}
      }
      if (matched) {
	std::copy(bv.begin(), bv.begin() + i, std::back_inserter(pattern));
	return true;
      }
    }
    return false;
  }  
  
public:
  bool claimLine(Addr addr, const std::array<bool, ChunkSize>& bv,
		 bool *upgrade, Addr *upgrade_addr, std::vector<bool> *upgrade_pat) {
    if (upgrade)
      *upgrade = false;
    
    // See if we have an existing line allocated for this.
    const unsigned idx = (addr / ChunkSize / LineSize) & (TableRows - 1);
    Row& row = rows[idx];
    if (Line *line = row.tryGetLine(addr / ChunkSize)) {
      if (line->tryMatchAndSet(addr / ChunkSize, bv.begin(), bv.end())) {
	++stat_match_success;

	// Are all the bits set?
	if (upgrade != nullptr && line->allSet()) {
	  *upgrade = true;
	  *upgrade_addr = line->getBaseAddr();
	  line->getPattern(std::back_inserter(*upgrade_pat));
	  line->invalidate();
	}
	
	return true;
      } else {
	++stat_match_fail;
	return false;
      }
    }

    // Otherwise, check if we find a pattern. If so, allocate a new line for it.
    std::vector<bool> pattern;
    if (hasPattern(bv, pattern)) {
      row.allocateLine(addr / ChunkSize, pattern.begin(), pattern.end(), stat_evictions);
      return true;
    }

    return false;
  }

  bool claimLine(Addr addr, const std::array<bool, ChunkSize>& bv) {
    return claimLine(addr, bv, nullptr, nullptr, nullptr);
  }

  void upgradePattern(Addr baseaddr, std::vector<bool>& pattern) {
    const unsigned idx = (baseaddr / ChunkSize / LineSize) & (TableRows - 1);
    Row& row = rows[idx];
    if (Line *line = row.tryGetLine(baseaddr / ChunkSize)) {
      std::array<bool, ChunkSize> buf;
      for (unsigned i = 0; i < buf.size(); i += pattern.size()) {
	std::copy_n(pattern.begin(), std::min<unsigned>(pattern.size(), buf.size() - i), buf.begin() + i);
      }
      line->tryMatchAndSet(baseaddr / ChunkSize, buf.begin(), buf.end());
      return;
    }

    // Otherwise, allocate new line.
    row.allocateLine(baseaddr / ChunkSize, pattern.begin(), pattern.end(), stat_evictions);
  }

  void printDesc(std::ostream& os) {
    os << "pattern declassification cache with chunk-size=" << ChunkSize << ", line-size=" << LineSize << ", assoc="
       << Associativity << ", num-lines=" << TableSize << "\n";
  }

  void printStats(std::ostream& os) {
    os << name << "-evictions " << stat_evictions << "\n";
    os << "pattern-match-success " << stat_match_success << "\n"
       << "pattern-match-fail " << stat_match_fail << "\n";
  }

  void dump(std::ostream& os) {
    for (const auto& row : rows)
      row.dump(os);
  }

  PatternDeclassificationCache(const std::string& name): name(name) {}

private:
  std::string name;
  std::array<Row, TableRows> rows;
  unsigned long stat_match_success = 0, stat_match_fail = 0, stat_evictions = 0;
};


template <unsigned ChunkSize, std::array<unsigned, 2> LineSizes, std::array<unsigned, 2> Associativities, std::array<unsigned, 2> TableSizes, unsigned MaxPatLen>
class MultiLevelPatternDeclassificationCache {
public:
  MultiLevelPatternDeclassificationCache(): lower("lower-pattern-cache"), upper("upper-pattern-cache") {}
  
  bool checkDeclassified(Addr addr, unsigned size) {
    if (lower.checkDeclassified(addr, size)) {
      ++stat_l2_hits;
      return true;
    } else if (upper.checkDeclassified(addr, size)) {
      ++stat_l3_hits;
      return true;
    } else {
      return false;
    }
  }

  bool setDeclassified(Addr addr, unsigned size, bool& downgrade, Addr& downgrade_addr, auto& downgrade_bv) {
    downgrade = false;
    return upper.setDeclassified(addr, size) || lower.setDeclassified(addr, size);
  }

  bool setClassified(Addr addr, unsigned size, Addr store_inst, bool& downgrade, Addr& downgrade_addr, auto& downgrade_bv) {
    downgrade = false;
    return upper.setClassified(addr, size, store_inst) || lower.setClassified(addr, size, store_inst);
  }

  bool claimLine(Addr addr, const std::array<bool, ChunkSize>& bv) {
    bool upgrade;
    Addr upgrade_addr;
    std::vector<bool> upgrade_pattern;
    const bool claimed = lower.claimLine(addr, bv, &upgrade, &upgrade_addr, &upgrade_pattern);
    if (upgrade) {
      upper.upgradePattern(upgrade_addr, upgrade_pattern);
      ++stat_level3_upgrades;
    }
    return claimed;
  }

  void printDesc(std::ostream& os) {
    lower.printDesc(os);
    upper.printDesc(os);
  }

  void printStats(std::ostream& os) {
    lower.printStats(os);
    upper.printStats(os);
    os << "level3-upgrades " << stat_level3_upgrades << "\n";
    os << "l2-hits " << stat_l2_hits << "\n";
    os << "l3-hits " << stat_l3_hits << "\n";
      
  }

  void dump(std::ostream& os) {
    os << "==== PATTERN LOWER ====\n";
    lower.dump(os);
    os << "\n\n\n==== PATTERN UPPER ====\n";
    upper.dump(os);
  }
  
private:
  PatternDeclassificationCache<ChunkSize, LineSizes[0], Associativities[0], TableSizes[0], MaxPatLen> lower;
  PatternDeclassificationCache<ChunkSize * LineSizes[0], LineSizes[1], Associativities[1], TableSizes[1], MaxPatLen> upper;
  unsigned long stat_level3_upgrades = 0;
  unsigned long stat_l3_hits = 0;
  unsigned long stat_l2_hits = 0;
};




template <unsigned ChunkSize_, unsigned MaxPatLen_, unsigned TableSize_>
class RealPatternDeclassificationTable {
public:
  static inline constexpr unsigned ChunkSize = ChunkSize_;
  static_assert((ChunkSize & (ChunkSize - 1)) == 0, "");
  static inline constexpr unsigned MaxPatLen = MaxPatLen_;
  static_assert(MaxPatLen <= ChunkSize, "");
  static inline constexpr unsigned TableSize = TableSize_;
  static_assert((TableSize & (TableSize - 1)) == 0, "");

  struct Line {
    bool valid;
    Addr tag;
    std::vector<bool> pattern;

    Line(): valid(false) {}
    template <typename InputIt>
    Line(Addr tag, InputIt pattern_first, InputIt pattern_last): valid(true), tag(tag), pattern(pattern_first, pattern_last) {}

    Addr baseaddr() const { return tag * ChunkSize; }

    bool contains(Addr addr) const {
      return valid && addr / ChunkSize == tag;
    }

    bool checkRange(Addr addr, unsigned size, bool value) const {
      assert(contains(addr));
      for (unsigned i = 0; i < size; ++i) {
	const unsigned pattern_idx = (addr + i - baseaddr()) % pattern.size();
	if (pattern[pattern_idx] != value)
	  return false;
      }
      return true;
    }

    bool checkDeclassified(Addr addr, unsigned size) {
      assert(contains(addr));
      return checkRange(addr, size, true);
    }

    void downgrade(bool& downgrade, Addr& downgrade_addr, std::array<bool, ChunkSize>& downgrade_bv) {
      downgrade = true;
      downgrade_addr = baseaddr();
      for (unsigned i = 0; i < downgrade_bv.size(); ++i) {
	downgrade_bv[i] = pattern[i % pattern.size()];
      }
    }

    bool setRange(Addr addr, unsigned size, bool value, bool& downgrade, Addr &downgrade_addr, std::array<bool, ChunkSize>& downgrade_bv) {
      if (checkRange(addr, size, value)) {
	return true;
      } else {
	this->downgrade(downgrade, downgrade_addr, downgrade_bv);
	return false;
      }
    }
  };

  bool checkDeclassified(Addr addr, unsigned size) {
    const Addr tag = addr / ChunkSize;
    const unsigned table_idx = tag & (TableSize - 1);
    Line& line = table[table_idx];
    return line.contains(addr) && line.checkDeclassified(addr, size);
  }

  bool setRange(Addr addr, unsigned size, bool value, bool& downgrade, Addr& downgrade_addr, std::array<bool, ChunkSize>& downgrade_bv) {
    downgrade = false;
    const Addr tag = addr / ChunkSize;
    const unsigned table_idx = tag & (TableSize - 1);
    Line& line = table[table_idx];
    if (line.contains(addr)) {
      return line.setRange(addr, size, value, downgrade, downgrade_addr, downgrade_bv);
    } else {
      return false;
    }
  }

  bool setDeclassified(Addr addr, unsigned size, bool& downgrade, Addr& downgrade_addr, std::array<bool, ChunkSize>& downgrade_bv) {
    return setRange(addr, size, true, downgrade, downgrade_addr, downgrade_bv);
  }

  bool setClassified(Addr addr, unsigned size, Addr store_inst, bool& downgrade, Addr& downgrade_addr, std::array<bool, ChunkSize>& downgrade_bv) {
    return setRange(addr, size, false, downgrade, downgrade_addr, downgrade_bv);
  }







private:
  template <typename OutputIt>
  bool hasPattern(const std::array<bool, ChunkSize>& bv, OutputIt pattern_out) {
    const unsigned max_pattern_length = std::min<unsigned>(MaxPatLen, bv.size());
    for (unsigned i = 1; i <= max_pattern_length; ++i) {
      bool matched = true;
      for (unsigned j = i; j < bv.size(); j += i) {
	const unsigned len = std::min<unsigned>(i, bv.size() - j);
	if (!std::equal(bv.begin(), bv.begin() + len,
			bv.begin() + j, bv.begin() + j + len)) {
	  matched = false;
	  break;
	}
      }
      if (matched) {
	std::copy(bv.begin(), bv.begin() + i, pattern_out);
	return true;
      }
    }
    return false;    
  }

public:

  bool claimLine(Addr addr, const std::array<bool, ChunkSize>& bv) {
    std::vector<bool> pattern;
    if (hasPattern(bv, std::back_inserter(pattern))) {
      // Add to table
      const Addr tag = addr / ChunkSize;
      const unsigned table_idx = tag & (TableSize - 1);
      Line& line = table[table_idx];
      if (line.valid) {
	assert(tag != line.tag);
	++stat_evictions;
      }
      line = Line(tag, pattern.begin(), pattern.end());
      ++stat_claimed;
      return true;
    } else {
      return false;
    }
  }

  
  
  void printDesc(std::ostream& os) {
    os << name << ": real-declassification-table with ChunkSize=" << ChunkSize << " MaxPatLen=" << MaxPatLen << " TableSize=" << TableSize << "\n";
  }

  void printStats(std::ostream& os) {
    os << name << ".evictions " << stat_evictions << "\n";
    os << name << ".claimed " << stat_claimed << "\n";
  }

  void dump(std::ostream& os) {}

  RealPatternDeclassificationTable(const std::string& name): name(name) {}
  
private:
  std::string name;
  std::array<Line, TableSize> table;
  unsigned long stat_evictions = 0;
  unsigned long stat_claimed = 0;
};
