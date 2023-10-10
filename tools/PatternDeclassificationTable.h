#pragma once

#include <vector>
#include <unordered_map>

#include "pin.H"

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

    Row(): tick(0) {
      std::fill(updated.begin(), updated.end(), tick);
    }

    void markUpdated(Lines::iterator it) {
      const unsigned idx = it - lines.begin();
      updated[idx] = ++tick;
    }

    // Stores the evicted line, if any, in @evicted. If no line
    // was evicted, then evicted.valid == false.
    Lines::iterator getOrAllocateLine(ADDRINT tag, Line& evicted) {
      evicted.valid = false;
      
      // First, check for matching line.
      for (auto it = lines.begin(); it != lines.end(); ++it)
	if (it->valid && it->tag == tag)
	  return it;

      const auto init_line = [&] (Lines::iterator it) {
	it->valid = true;
	it->tag = tag;
	std::fill(it->bv.begin(), it->bv.end(), false);
	updated[it - lines.begin()] = tick;
      };

      // Check for free line.
      for (auto it = lines.begin(); it != lines.end(); ++it) {
	if (!it->valid) {
	  init_line(it);
	  return it;
	}
      }

      // Check for least-recently-updated line.
      Tick lru_tick = std::numeric_limits<Tick>::max();
      typename Lines::iterator lru_it = lines.end();
      for (auto it = lines.begin(); it != lines.end(); ++it) {
	assert(it->valid);
	const unsigned idx = it - lines.begin();
	if (updated[idx] <= lru_tick) {
	  lru_tick = updated[idx];
	  lru_it = it;
	}
      }
      assert(lru_it != lines.end());

      evicted = *lru_it;
      init_line(lru_it);
      return lru_it;
    }

    bool tryGetLine(ADDRINT tag, typename Lines::iterator& out) {
      for (typename Lines::iterator it = lines.begin(); it != lines.end(); ++it) {
	if (it->valid && it->tag == tag) {
	  out = it;
	  return true;
	}
      }
      return false;
    }
    

  private:
    Lines lines;
    Tick tick;
    std::array<Tick, TableCols> updated;
  };

  bool checkDeclassified(ADDRINT addr, unsigned size) {
    const unsigned line_off = addr & (LineSize - 1);
    const ADDRINT tag = addr / LineSize;
    const unsigned row_idx = tag & (TableRows - 1);
    Row& row = rows[row_idx];

    typename Row::Lines::iterator row_it;
    if (!row.tryGetLine(tag, row_it))
      return false;

    const Line& line = *row_it;
    assert(line.valid);
    return line.allSet(line_off, size);
  }

  // NOTE: This function should only be called if we are certain that an upper table doesn't
  // indicate this range is declassified, or else we'll allocate needless entries in this
  // cache.
  void setDeclassified(ADDRINT addr, unsigned size, Line& evicted) {
    const unsigned line_off = addr & (LineSize - 1);
    const ADDRINT tag = addr / LineSize;
    const unsigned row_idx = tag & (TableRows - 1);
    Row& row = rows[row_idx];
    auto row_it = row.getOrAllocateLine(tag, evicted);
    Line& line = *row_it;
    const bool updated = line.set(line_off, size);
    if (updated)
      row.markUpdated(row_it);
  }

  void setClassified(ADDRINT addr, unsigned size) {
    const unsigned line_off = addr & (LineSize - 1);
    const ADDRINT tag = addr / LineSize;
    const unsigned row_idx = tag & (TableRows - 1);
    Row& row = rows[row_idx];
    typename Row::Lines::iterator row_it;
    if (!row.tryGetLine(tag, row_it))
      return;
    Line& line = *row_it;
    if (line.reset(line_off, size))
      row.markUpdated(row_it);

    // If the line is now empty, just deallocate it.
    if (line.allReset())
      line.valid = false;
  }

private:
  std::array<Row, TableRows> rows;
};




template <unsigned LineSize_, unsigned Associativity_, unsigned TableSize_, unsigned DictSize_>
class PatternDeclassificationTable {
public:
  static inline constexpr unsigned LineSize = LineSize_;
  static_assert((LineSize & (LineSize - 1)) == 0, "");
  static inline constexpr unsigned Associativity = Associativity_;
  static inline constexpr unsigned TableSize = TableSize_;
  static inline constexpr unsigned TableCols = Associativity;
  static inline constexpr unsigned TableRows = TableSize / TableCols;
  static_assert((TableRows & (TableRows - 1)) == 0, "");
  static inline constexpr unsigned DictSize = DictSize_;
  static inline constexpr unsigned WordSize = LineSize;

  using SeqNum = unsigned;

  struct Word {
    unsigned refcnt;
    SeqNum id;
    std::array<bool, WordSize> bv;

    Word(): refcnt(0) {}
    
    bool valid() const { return refcnt > 0; }

    bool allSet(unsigned first, unsigned size) const {
      return std::reduce(bv.begin() + first,
			 bv.begin() + first + size,
			 true,
			 std::logical_and<bool>());
    }

    bool anySet() const {
      return std::reduce(bv.begin(), bv.end(), false, std::logical_or<bool>());
    }

    bool anySet(unsigned first, unsigned size) const {
      return std::reduce(bv.begin() + first,
			 bv.begin() + first + size,
			 false,
			 std::logical_or<bool>());
    }

    bool allReset() const {
      return !anySet();
    }

    bool allReset(unsigned first, unsigned size) const {
      return !anySet(first, size);
    }
  };

  using WordIdx = unsigned;

  class Dictionary {
  public:
    Dictionary(): seq_num(0) {}
    
    WordIdx getOrAllocateWord(const std::array<bool, LineSize>& bv) {
      // Try to find existing word.
      for (WordIdx i = 0; i < words.size(); ++i) {
	Word& word = words[i];
	if (word.valid() && word.bv == bv) {
	  ++word.refcnt;
	  return i;
	}
      }

      // Otherwise, evict the word with the lowest reference count (may be zero).
      const auto it = std::min_element(words.begin(), words.end(), [] (const Word& a, const Word& b) -> bool {
	return a.refcnt < b.refcnt;
      });
      assert(it != words.end());
      it->refcnt;
      if (it->valid())
	++seq_num;
      it->id = seq_num;
      it->refcnt = 1;
      it->bv = bv ;
      return it - words.begin();
    }

    Word& operator[](WordIdx word_idx) {
      if (word_idx >= words.size())
	fprintf(stderr, "got index %u\n", word_idx);
      assert(word_idx < words.size());
      return words[word_idx];
    }
    
  private:
    SeqNum seq_num;
    std::array<Word, DictSize> words;
  };

  struct Line {
    bool valid;
    ADDRINT tag;
    std::array<bool, LineSize> mask;
    std::array<WordIdx, LineSize> idxs;
    std::array<SeqNum, LineSize> seqs;

    Line(): valid(false) {}

    void recomputeValid() {
      valid = std::reduce(mask.begin(), mask.end(), false, std::logical_or<bool>());
    }
  };

  class Row {
  public:
    using Lines = std::array<Line, TableCols>;

    // NOTE: Identical implementation as in cache.
    bool tryGetLine(ADDRINT tag, Lines::iterator& out) {
      for (auto it = lines.begin(); it != lines.end(); ++it) {
	if (it->valid && it->tag == tag) {
	  out = it;
	  return true;
	}
      }
      return false;
    }

    Line& evictLine() {
      const auto it = std::min_element(lines.begin(), lines.end(), [] (const Line& a, const Line& b) {
	const auto f = [] (const Line& line) -> unsigned {
	  return std::count(line.mask.begin(), line.mask.end(), true);
	};
	return f(a) < f(b);
      });
      assert(it != lines.end());
      return *it;
    }

  private:
    Lines lines;
  };

private:
  bool checkDeclassifiedExtra(ADDRINT addr, unsigned size, bool& miss, Line **line_ptr, bool **mask_ptr, Word **word_ptr) {
    const unsigned word_off = addr & (WordSize - 1);
    const unsigned line_off = (addr / WordSize) & (LineSize - 1);
    const unsigned tag = addr / (WordSize * LineSize);
    const unsigned row_idx = tag & (TableRows - 1);
    Row& row = rows[row_idx];

    typename Row::Lines::iterator row_it;
    if (!row.tryGetLine(tag, row_it)) {
      miss = true;
      return false;
    }

    Line& line = *row_it;
    assert(line.valid);

    // Check if the corresponding word index is valid.
    if (!line.mask[line_off]) {
      miss = true;
      return false;
    }

    // Check if the word's sequence number matches.
    Word& word = dict[line.idxs[line_off]];
    if (!word.valid() || word.id != line.seqs[line_off]) {
      // Invalidate this index.
      line.mask[line_off] = false;
      miss = true;
      return false;
    }

    miss = false;
    if (line_ptr)
      *line_ptr = &line;
    if (mask_ptr)
      *mask_ptr = &line.mask[line_off];
    if (word_ptr)
      *word_ptr = &word;
    return word.allSet(word_off, size);
  }

public:

  bool checkDeclassified(ADDRINT addr, unsigned size) {
    bool miss;
    return checkDeclassifiedExtra(addr, size, miss, nullptr, nullptr, nullptr);
  }

  // TODO: Refactor the shared code with checkDeclassified().
  bool setDeclassified(ADDRINT addr, unsigned size) {
    bool miss;
    Line *line;
    bool *mask_ptr;
    Word *word_ptr;
    if (checkDeclassifiedExtra(addr, size, miss, &line, &mask_ptr, &word_ptr))
      return true;

    if (miss)
      return false;

    // Otherwise, we had a hit but the right bits weren't set in the word.
    // For now, we will just deallocate this word idx.
    *mask_ptr = false;
    assert(word_ptr->refcnt > 0);
    --word_ptr->refcnt;
    line->recomputeValid();
    
    // It's not declassified yet, which means we need to eliminate
    // or downgrade this line.
    // Simply invalidate it for now.
    stat_declassify_invalidate++;
    
    return false;
  }

  void setClassified(ADDRINT addr, unsigned size) {
    const unsigned word_off = addr & (WordSize - 1);
    const unsigned line_off = (addr / WordSize) & (LineSize - 1);
    const unsigned tag = addr / (WordSize * LineSize);
    const unsigned row_idx = tag & (TableRows - 1);
    Row& row = rows[row_idx];

    typename Row::Lines::iterator row_it;
    if (!row.tryGetLine(tag, row_it)) {
      return;
    }

    Line& line = *row_it;
    assert(line.valid);

    // Check if the corresponding word index is valid.
    if (!line.mask[line_off]) {
      return;
    }

    // Check if the word's sequence number matches.
    Word& word = dict[line.idxs[line_off]];
    if (!word.valid() || word.id != line.seqs[line_off]) {
      // Invalidate this index.
      line.mask[line_off] = false;
      return;
    }

    if (word.allReset(word_off, size))
      return;

    // If we got here, we have a word allocated on which we're
    // clearing bits. The simple solution for now is to
    // invalidate the word index in the line.
    line.mask[line_off] = false;
    assert(word.refcnt > 0);
    --word.refcnt;
    line.recomputeValid();

    stat_classify_invalidate++;
  }

  void upgradeLine(const std::array<bool, LineSize>& bv, ADDRINT addr) {
    // Put the word in the dictionary.
    const WordIdx word_idx = dict.getOrAllocateWord(bv);

    // Access line.
    const unsigned line_off = addr & (LineSize - 1);
    const unsigned tag = addr / LineSize;
    const unsigned row_idx = tag & (TableRows - 1);
    Row& row = rows[row_idx];

    typename Row::Lines::iterator row_it;
    if (!row.tryGetLine(tag, row_it)) {
      // Evict existing line. This is a bit tricky.
      // Current metric: minumum number of valid word indices.
      Line& line = row.evictLine();
      for (unsigned i = 0; i < LineSize; ++i) {
	if (line.mask[i]) {
	  // Need to decrement reference counter.
	  const WordIdx word_idx = line.idxs[i];
	  Word& word = dict[word_idx];
	  if (word.valid() && word.id == line.seqs[i])
	    --word.refcnt;
	  // TODO: Should use smart pointers to automate this crap.
	}
      }
      line.mask[line_off] = true;
      line.idxs[line_off] = word_idx;
      line.tag = tag;
      line.seqs[line_off] = dict[word_idx].id;
      line.valid = true;
    }
  }

  void printStats(std::ostream& os) {
    os << "pattern-declassify-invalidate " << stat_declassify_invalidate << "\n"
       << "pattern-classify-invalidate " << stat_classify_invalidate << "\n";
  }

private:
  Dictionary dict;
  std::array<Row, TableRows> rows;
  unsigned long stat_declassify_invalidate = 0;
  unsigned long stat_classify_invalidate = 0;
};







// template <unsigned LineSize_, unsigned Associativity_, unsigned TableSize_, unsigned DictSize_>
// class PatternDeclassificationTable {

template <unsigned LineSize, unsigned Associativity, unsigned TableSize, unsigned DictSize>
class DeclassificationTable {
public:
  using Cache = DeclassificationCache<LineSize, Associativity, TableSize>;
  using PatternTable = PatternDeclassificationTable<LineSize, Associativity, TableSize, DictSize>;
  
  DeclassificationTable(const char *eviction_path) {
    if ((eviction_file = fopen(eviction_path, "w")) == NULL) {
      perror("fopen");
      exit(1);
    }
  }

  bool checkDeclassified(ADDRINT addr, unsigned size) {
#if 1
    if (pattern_table.checkDeclassified(addr, size)) {
      ++stat_pattern_hit;
      return true;
    }
#endif
    if (cache.checkDeclassified(addr, size)) {
      ++stat_cache_hit;
      return true;
    }
    return false;
  }
    
  void setDeclassified(ADDRINT addr, unsigned size) {
#if 1
    if (pattern_table.setDeclassified(addr, size))
      return;
#endif

    typename Cache::Line evicted;
    cache.setDeclassified(addr, size, evicted);
    if (evicted.valid) {
      // Move to pattern table
      pattern_table.upgradeLine(evicted.bv, addr / LineSize);
      ++stat_upgrades;

#if 0
      assert(evicted.anySet());
      for (unsigned i = 0; i < evicted.bv.size(); i += 8) {
	uint8_t value = 0;
	for (unsigned j = 0; j < 8; ++j) {
	  value <<= 1;
	  if (evicted.bv[i + j])
	    value |= 1;
	}
	fprintf(eviction_file, "%02hhx", value);
      }
      fprintf(eviction_file, "\n");
#endif
    }
  }

  void setClassified(ADDRINT addr, unsigned size, ADDRINT) {
#if 1
    pattern_table.setClassified(addr, size);
#endif
    cache.setClassified(addr, size);
  }

  void printDesc(std::ostream& os) {
    os << "Configuration: pattern declassification table\n";
  }

  void printStats(std::ostream& os) {
    os << "pattern-hit " << stat_pattern_hit << "\n"
       << "cache-hit " << stat_cache_hit << "\n"
       << "upgrades " << stat_upgrades << "\n";
    pattern_table.printStats(os);
  }

private:
  Cache cache;
  PatternTable pattern_table;
  FILE *eviction_file;
  unsigned long stat_pattern_hit = 0;
  unsigned long stat_cache_hit = 0;
  unsigned long stat_upgrades = 0;
};
