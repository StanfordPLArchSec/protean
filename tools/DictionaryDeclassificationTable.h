#pragma once

#include <vector>
#include <unordered_map>
#include <cstdio>
#include <iostream>

#include "pin.H"

static inline bool was_pattern_hit;

static inline void write_bv(std::ostream& os, const auto& bv) {
  assert(bv.size() % 8 == 0);
  for (unsigned i = 0; i < bv.size(); i += 8) {
    uint8_t mask = 0;
    for (unsigned j = 0; j < 8; ++j) {
      mask <<= 1;
      if (bv[i + j])
	mask |= 1;
    }
    char buf[16];
    sprintf(buf, "%02hhx", mask);
    os << buf;
  }
}

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

  class Word {
  private:
    bool valid_;
  public:
    // TODO: Make private.
    std::array<bool, WordSize> bv;

  public:
    Word(const std::array<bool, WordSize>& bv): valid_(true), bv(bv) {}

    bool valid() const { return valid_; }
    void invalidate() {
      assert(valid());
      valid_ = false;
    }

    bool matches(const std::array<bool, WordSize>& other_bv) const {
      return valid() && bv == other_bv;
    }

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

    void set(unsigned first, unsigned size) {
      std::fill_n(bv.begin() + first, size, true);
    }

    bool reset(unsigned first, unsigned size) {
      std::fill_n(bv.begin() + first, size, false);
      return allReset();
    }

  };

  using WordPtr = std::shared_ptr<Word>;

  class Dictionary {
  public:
    Dictionary() {}

    WordPtr getOrAllocateWord(const std::array<bool, LineSize>& bv) {
      // Try to find existing word.
      for (auto& weak_word : words) {
	WordPtr word = weak_word.lock();
	if (word && word->matches(bv))
	  return word;
      }

      // Try to allocate over an invalid word.
      for (auto& word : words) {
	if (word.expired() || !word.lock()->valid()) {
	  WordPtr new_word = std::make_shared<Word>(bv);
	  word = new_word;
	  return new_word;
	}
      }

      // Otherwise, evict the word with the lowest reference count.
      const auto it = std::min_element(words.begin(), words.end(), [] (const auto& a, const auto& b) -> bool {
	return a.use_count() < b.use_count(); 
      });
      assert(it != words.end() && !it->expired());
      WordPtr new_word = std::make_shared<Word>(bv);
      it->lock()->invalidate(); // Invalidate existing word there.
      ++stat_invalidations;
      stat_invalidation_refcnt += it->use_count();
      *it = new_word;
      return new_word;
      }

    void printStats(std::ostream& os) {
      os << "word-invalidations " << stat_invalidations << "\n";
      os << "word-invalidation-refcnt " << stat_invalidation_refcnt << "\n";
      for (unsigned i = 0; i < words.size(); ++i) {
	const auto& word = words[i];
	os << "word" << i << " refcnt=" << word.use_count() << " ";
	if (word.expired()) {
	  os << "(invalid)";
	} else {
	  write_bv(os, word.lock()->bv);
	}
	os << "\n";
      }
    }

  private:
    std::array<std::weak_ptr<Word>, DictSize> words;
    unsigned long stat_invalidations = 0;
    unsigned long stat_invalidation_refcnt = 0;
  };

  struct Line {
    ADDRINT tag;
    std::array<WordPtr, LineSize> words;
    
    bool valid() const {
      return std::any_of(words.begin(), words.end(), [] (const WordPtr& word) -> bool {
	return word != nullptr;
      });
    }

    void init(ADDRINT tag) {
      this->tag = tag;
      std::fill(words.begin(), words.end(), nullptr);
    }
  };

  class Row {
  public:
    using Lines = std::array<Line, TableCols>;

    // NOTE: Identical implementation as in cache.
    Line *tryGetLine(ADDRINT tag) {
      for (auto it = lines.begin(); it != lines.end(); ++it) {
	if (it->valid() && it->tag == tag) {
	  return &*it;
	}
      }
      return nullptr;
    }

    Line& evictLine(bool& evicted) {
      // Any invalid?
      for (Line& line : lines) {
	if (!line.valid()) {
	  evicted = false;
	  return line;
	}
      }
      
      const auto it = std::max_element(lines.begin(), lines.end(), [] (const Line& a, const Line& b) {
	const auto f = [] (const Line& line) -> unsigned {
	  assert(line.valid());
	  return std::count(line.words.begin(), line.words.end(), nullptr);
	};
	return f(a) < f(b);
      });
      assert(it != lines.end());
      evicted = true;
      return *it;
    }

  private:
    Lines lines;
  };

public:
  bool checkDeclassified(ADDRINT addr, unsigned size) {
    const unsigned word_off = addr & (WordSize - 1);
    const unsigned line_off = (addr / WordSize) & (LineSize - 1);
    const ADDRINT tag = addr / (WordSize * LineSize);
    const unsigned row_idx = tag & (TableRows - 1);
    Row& row = rows[row_idx];

    Line *line = row.tryGetLine(tag);
    if (line == nullptr)
      return false;

    assert(line->valid());

    // Check if the corresponding word at the index is valid.
    WordPtr& word = line->words[line_off];
    if (!(word && word->valid()))
      return false;

    return word->allSet(word_off, size);
  }

public:

  bool setDeclassified(ADDRINT addr, unsigned size) {
    const unsigned word_off = addr & (WordSize - 1);
    const unsigned line_off = (addr / WordSize) & (LineSize - 1);
    const ADDRINT tag = addr / (WordSize * LineSize);
    const unsigned row_idx = tag & (TableRows - 1);
    Row& row = rows[row_idx];

    Line *line = row.tryGetLine(tag);
    if (line == nullptr)
      return false; // We will let the cache handle this one.
    
    assert(line->valid());

    // Check if all bits are set.
    WordPtr& word = line->words[line_off];
    if (!(word && word->valid()))
      return false; // Let cache handle this as well.

    if (word->allSet(word_off, size))
      return true;

    if (word.use_count() == 1) {
#if 0
      // We own this word, so we can update it as we choose.
      word->set(word_off, size);
      ++stat_declassify_invalidations_owned;
      return true;
#else
      ++stat_declassify_invalidations_owned;
#endif
    }

    // Otherwise, some bits weren't set.
    // For now, handle this by invalidating the word.
    word = nullptr;
    ++stat_declassify_invalidations;
    return false;
  }

  bool setClassified(ADDRINT addr, unsigned size) {
    const unsigned word_off = addr & (WordSize - 1);
    const unsigned line_off = (addr / WordSize) & (LineSize - 1);
    const ADDRINT tag = addr / (WordSize * LineSize);
    const unsigned row_idx = tag & (TableRows - 1);
    Row& row = rows[row_idx];

    Line *line = row.tryGetLine(tag);
    if (line == nullptr)
      return false;

    assert(line->valid());

    // Check if all bits are already reset.
    WordPtr& word = line->words[line_off];
    if (!(word && word->valid()))
      return false;

    if (word->allReset(word_off, size))
      return true;

    // Otherwise, some bits were set.
    // For now, handle this by invalidating the word.
    if (word.use_count() == 1)
      ++stat_classify_invalidations_owned;
    word = nullptr;
    ++stat_classify_invalidations;
    return true;
  }

  void upgradeLine(const std::array<bool, LineSize>& bv, ADDRINT addr) {
    // Get the word.
    WordPtr word = dict.getOrAllocateWord(bv);

    // Access line.
    const unsigned line_off = addr & (LineSize - 1);
    const ADDRINT tag = addr / LineSize;
    const unsigned row_idx = tag & (TableRows - 1);
    Row& row = rows[row_idx];

    // fprintf(stderr, "upgradeLine %lx-%lx\n", tag * LineSize * WordSize, (tag + 1) * LineSize * WordSize);

    Line *line = row.tryGetLine(tag);
    if (line == nullptr) {
      // Evict existing line. This is a bit tricky.
      // Current metric: minumum number of valid word indices.
      bool evicted;
      line = &row.evictLine(evicted);
      if (line->valid()) {
	++stat_t1_evictions;
	stat_t1_eviction_wordcnt += std::count_if(line->words.begin(), line->words.end(), [] (const WordPtr& word) {
	  return word && word->valid();
	});
      }

      line->init(tag);
    }

    // fprintf(stderr, "set-word %u %lx-%lx\n", line_off, tag * LineSize * WordSize + line_off * WordSize,
    // tag * LineSize * WordSize + (line_off + 1) * WordSize);
    line->words[line_off] = std::move(word);
    
    ++stat_t0_upgrades;
  }

  void printStats(std::ostream& os) {
    os << "t0->t1-upgrades " << stat_t0_upgrades << "\n";
    os << "t1-evictions " << stat_t1_evictions << "\n";
    os << "t1-eviction-wordcnt " << stat_t1_eviction_wordcnt << "\n";
    os << "t1-declassify-invalidations " << stat_declassify_invalidations << "\n";
    os << "t1-classify-invalidations " << stat_classify_invalidations << "\n";
    os << "t1-declassify-invalidations-owned " << stat_declassify_invalidations_owned << "\n";
    os << "t1-classify-invalidations-owned " << stat_classify_invalidations_owned << "\n";
    os << "dictionary:\n";
    dict.printStats(os);
  }

private:
  Dictionary dict;
  std::array<Row, TableRows> rows;
  unsigned long stat_t0_upgrades = 0; 
  unsigned long stat_t1_evictions = 0;
  unsigned long stat_t1_eviction_wordcnt = 0;
  unsigned long stat_declassify_invalidations = 0;
  unsigned long stat_classify_invalidations = 0;
  unsigned long stat_declassify_invalidations_owned = 0;
  unsigned long stat_classify_invalidations_owned = 0;
};







// template <unsigned LineSize_, unsigned Associativity_, unsigned TableSize_, unsigned DictSize_>
// class PatternDeclassificationTable {

template <unsigned LineSize, unsigned Associativity, unsigned TableSize, unsigned DictSize, bool EnablePattern>
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
    // fprintf(stderr, "checkDeclassified %lx %u\n", addr, size);
    was_pattern_hit = false;
    if constexpr (EnablePattern) {
      if (pattern_table.checkDeclassified(addr, size)) {
	++stat_pattern_hit;
	was_pattern_hit = true;
	return true;
      }
    }
    if (cache.checkDeclassified(addr, size)) {
      ++stat_cache_hit;
      return true;
    }
    return false;
  }
    
  void setDeclassified(ADDRINT addr, unsigned size) {
    // fprintf(stderr, "setDeclassified %lx %u\n", addr, size);
    if constexpr (EnablePattern) {
      if (pattern_table.setDeclassified(addr, size))
	return;
    }

    typename Cache::Line evicted;
    cache.setDeclassified(addr, size, evicted);
    if (evicted.valid) {
      // Move to pattern table
      // fprintf(stderr, "upgrading %lx %u\n", evicted.tag * LineSize, LineSize);
      pattern_table.upgradeLine(evicted.bv, evicted.tag);
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
    // fprintf(stderr, "setClassified %lx %u\n", addr, size);
    if constexpr (EnablePattern) {
      pattern_table.setClassified(addr, size);
    }
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
