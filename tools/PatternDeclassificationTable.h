#pragma once

#include <algorithm>
#include <array>

#include "Util.h"
#include "ShadowMemory.h"

template <unsigned LineSize_>
class IdealPatternDeclassificationTable {
public:
  static inline constexpr unsigned LineSize = LineSize_;
  static_assert((LineSize & (LineSize - 1)) == 0, "");
  
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

  bool setDeclassified(Addr addr, unsigned size) {
    Type *ptr = &shadow[addr];
    if (checkAllDeclassified(ptr, ptr + size))
      return true;
    invalidateLine(addr);
    return false;
  }

  void setClassified(Addr addr, unsigned size, Addr store_inst) {
    Type *ptr = &shadow[addr];
    if (!checkAnyDeclassified(ptr, ptr + size))
      return;
    invalidateLine(addr);
  }

private:
  bool hasPattern(const std::array<bool, LineSize>& bv, std::vector<bool>& pattern) {
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






template <unsigned LineSize_, unsigned Associativity_, unsigned TableSize_>
class PatternDeclassificationCache {
public:
  static inline constexpr unsigned LineSize = LineSize_;
  static_assert((LineSize & (LineSize - 1)) == 0, "");
  static inline constexpr unsigned Associativity = Associativity_;
  static inline constexpr unsigned TableCols = Associativity;
  static inline constexpr unsigned TableSize = TableSize_;
  static inline constexpr unsigned TableRows = TableSize / TableCols;
  static_assert(TableSize % TableCols == 0, "");
  static_assert((TableRows & (TableRows - 1)) == 0, "");
  static inline constexpr unsigned MaxPatLen = 16;

  using PatBV = std::array<bool, LineSize*2>;
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
      const Addr lower_baseaddr = lower_tag * LineSize;
      const Addr baseaddr = tag_ * LineSize * LineSize;
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

    // Returns whether the match was successful.
    template <typename InputIt>
    bool tryMatchAndSet(Addr lower_tag, InputIt bv_first, InputIt bv_last) {
      assert(bv_last - bv_first == LineSize);
      assert(contains(lower_tag * LineSize));

      // Compute the pattern shift.
      const Addr lower_baseaddr = lower_tag * LineSize;
      const Addr baseaddr = tag_ * LineSize * LineSize;
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

      const Addr tag = addr / LineSize / LineSize;
      if (tag != tag_)
	return false;

      return true;
    }

  private:
    bool check(Addr addr, unsigned size, bool check) {
      assert(contains(addr));

      const unsigned line_idx = (addr / LineSize) & (LineSize - 1);
      if (!bv_[line_idx])
	return false;

      // Compute pattern offset.
      const Addr baseaddr = tag_ * LineSize * LineSize;
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
      const unsigned line_idx = (addr / LineSize) & (LineSize - 1);
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
      sprintf(buf, "addr=%016lx patlen=%u pattern=%s data=%s",
	      tag_ * LineSize * LineSize,
	      patlen_,
	      bv_to_string1(pat_.begin(), pat_.begin() + patlen_).c_str(),
	      bv_to_string8(bv_.begin(), bv_.end()).c_str()
	      );
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
	if (line.contains(lower_tag * LineSize)) {
	  return &line;
	}
      }
      return nullptr;
    }

    template <typename InputIt>
    Line& allocateLine(Addr lower_tag, InputIt pat_first, InputIt pat_last) {
      assert(tryGetLine(lower_tag) == nullptr);
      
      // Try to find invalid/empty line.
      for (Line& line : lines) {
	if (!line.valid()) {
	  line = Line(lower_tag, pat_first, pat_last);
	  assert(line.contains(lower_tag * LineSize));
	  return line;
	}
      }
      
      // Fall back to random eviction policy for now.
      const unsigned row_idx = std::rand() % lines.size();
      Line& line = lines[row_idx];
      line = Line(lower_tag, pat_first, pat_last);
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
    const unsigned idx = (addr / LineSize / LineSize) & (TableRows - 1);
    Row& row = rows[idx];
    return row.checkDeclassified(addr, size);
  }

  bool setDeclassified(Addr addr, unsigned size) {
    const unsigned idx = (addr / LineSize / LineSize) & (TableRows - 1);
    Row& row = rows[idx];
    return row.setDeclassified(addr, size);
  }

  bool setClassified(Addr addr, unsigned size, Addr store_inst) {
    const unsigned idx = (addr / LineSize / LineSize) & (TableRows - 1);
    Row& row = rows[idx];
    return row.setClassified(addr, size);
  }

private:

  bool hasPattern(const std::array<bool, LineSize>& bv, std::vector<bool>& pattern) {
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
  bool claimLine(Addr addr, const std::array<bool, LineSize>& bv) {
    // See if we have an existing line allocated for this.
    const unsigned idx = (addr / LineSize / LineSize) & (TableRows - 1);
    Row& row = rows[idx];
    if (Line *line = row.tryGetLine(addr / LineSize)) {
      if (line->tryMatchAndSet(addr / LineSize, bv.begin(), bv.end())) {
	++stat_match_success;
	return true;
      } else {
	++stat_match_fail;
#if 0
	std::cerr << "PATTERN MATCH FAIL: ";
	line->dump(std::cerr);
	std::cerr << " our-data=" << bv_to_string1(bv.begin(), bv.end()) << "\n";
#endif
	return false;
      }
    }

    // Otherwise, check if we find a pattern. If so, allocate a new line for it.
    std::vector<bool> pattern;
    if (hasPattern(bv, pattern)) {
      row.allocateLine(addr / LineSize, pattern.begin(), pattern.end());
      return true;
    }

    return false;
  }

  void printDesc(std::ostream& os) {
    os << "pattern declassification cache with line-size=" << LineSize << ", assoc="
       << Associativity << ", num-lines=" << TableSize << "\n";
  }

  void printStats(std::ostream& os) {
    os << "pattern-match-success " << stat_match_success << "\n"
       << "pattern-match-fail " << stat_match_fail << "\n";
  }

  void dump(std::ostream& os) {
    for (const auto& row : rows)
      row.dump(os);
  }

private:
  std::array<Row, TableRows> rows;
  unsigned long stat_match_success = 0, stat_match_fail = 0;
};
