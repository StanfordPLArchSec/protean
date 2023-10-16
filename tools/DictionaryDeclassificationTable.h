#pragma once

#include <vector>
#include <unordered_map>
#include <cstdio>
#include <iostream>

#include "pin.H"

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

    unsigned popCount() const {
      return std::count(bv.begin(), bv.end(), true);
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
	const auto score = [] (const auto& word) -> int {
	  const auto uses = word.use_count();
	  if (word.expired()) {
	    return 0;
	  } else {
	    const auto word_ = word.lock();
	    return uses * std::count(word_->bv.begin(), word_->bv.end(), true);
	  }
	};
	return score(a) < score(b);
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
	  const auto& bv = word.lock()->bv;
	  for (unsigned j = 0; j < bv.size(); j += 8) {
	    uint8_t value = 0;
	    for (unsigned k = 0; k < 8; ++k) {
	      value <<= 1;
	      if (bv[j + k])
		value |= 1;
	    }
	    char buf[16];
	    sprintf(buf, "%02hhx", value);
	    os << buf;
	  }
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
	return word && word->valid();
      });
    }

    void init(ADDRINT tag) {
      this->tag = tag;
      std::fill(words.begin(), words.end(), nullptr);
    }

    void dump(std::ostream& os) const {
      if (valid()) {
	char buf[256];
	sprintf(buf, "%016lx ", tag);
	os << buf;
	for (bool first = true; const auto& word : words) {
	  if (first) {
	    first = false;
	  } else {
	    os << ",";
	  }
	  if (word && word->valid()) {
	    // Get index into table
	    os << bv_to_string(word->bv.begin(), word->bv.end());
	  } else {
	    os << "xxxxxxxxxxxxxxxx";
	  }
	}
      } else {
	os << "xxxxxxxxxxxxxxxx xxxxxxxxxxxxxxxx\n";
      }
    }
  };

  class Row {
  public:
    using Lines = std::array<Line, TableCols>;

    Row(): tick(0) {
      std::fill(used.begin(), used.end(), tick);
    }
      

    // NOTE: Identical implementation as in cache.
    Line *tryGetLine(ADDRINT tag) {
      for (auto it = lines.begin(); it != lines.end(); ++it) {
	if (it->valid() && it->tag == tag) {
	  return &*it;
	}
      }
      return nullptr;
    }

    void markUsed(Line& line) {
      ++tick;
      used[&line - lines.data()] = tick;
    }

    Line& evictLine(bool& evicted) {
      // Any invalid?
      for (Line& line : lines) {
	if (!line.valid()) {
	  evicted = false;
	  return line;
	}
      }

      const auto lru_metric = [&] (const Line& line) -> int {
	return used[&line - lines.data()];
      };

      const auto metric = [&] (const Line& line) -> int {
	return std::rand();
	// return lru_metric(line) + popcnt_metric(line);
      };

      const auto it = std::min_element(lines.begin(), lines.end(), [&] (const Line& a, const Line& b) {
	return metric(a) < metric(b);
      });
      assert(it != lines.end());
      evicted = true;
      return *it;
    }

    void dump(std::ostream& os) const {
      for (const auto& line : lines) {
	line.dump(os);
	os << "\n";
      }
    }

    using Tick = unsigned;

  private:
    Lines lines;
    Tick tick = 0;
    std::array<Tick, TableCols> used;
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
    
    const bool is_declassified = word->allSet(word_off, size);
    
    if (is_declassified)
      row.markUsed(*line);

    return is_declassified;
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
	
	// fprintf(stderr, "evicted %s for %s\n", bv_to_string(line->bv, 
      }

      line->init(tag);
    }

    // fprintf(stderr, "set-word %u %lx-%lx\n", line_off, tag * LineSize * WordSize + line_off * WordSize,
    // tag * LineSize * WordSize + (line_off + 1) * WordSize);
    line->words[line_off] = std::move(word);
    assert(line->valid());
    
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

  void dump(std::ostream& os) {
    for (const auto& row : rows) {
      row.dump(os);
    }
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







template <unsigned LineSize, unsigned Associativity, unsigned TableSize, unsigned DictSize, bool EnablePattern, class Cache>
class DictionaryDeclassificationTable {
public:
  // using Cache = DictionaryDeclassificationCache<LineSize, Associativity, TableSize>;
  // using Cache = DeclassificationCache<LineSize, Associativity, TableSize>;
  using PatternTable = PatternDeclassificationTable<LineSize, Associativity, TableSize, DictSize>;
  
  DictionaryDeclassificationTable(Cache& cache): cache(cache) {}

  bool checkDeclassified(ADDRINT addr, unsigned size) {
    if constexpr (EnablePattern) {
      if (pattern_table.checkDeclassified(addr, size)) {
	++stat_pattern_hit;
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

    bool evicted;
    ADDRINT evicted_tag;
    std::array<bool, LineSize> evicted_bv;
    cache.setDeclassified(addr, size, evicted, evicted_tag, evicted_bv);
    evicted_tag /= LineSize;
    if constexpr (EnablePattern) {
      if (evicted) {
	const auto popcnt = std::count(evicted_bv.begin(), evicted_bv.end(), true);
	if (popcnt >= 4) {
	  // Move to pattern table
	  // fprintf(stderr, "upgrading %lx %u\n", evicted.tag * LineSize, LineSize);
	  pattern_table.upgradeLine(evicted_bv, evicted_tag);
	  ++stat_upgrades;
	} else {
	  ++stat_discards;
	}
      }
    }
  }

  void setClassified(ADDRINT addr, unsigned size, ADDRINT store_inst) {
    // fprintf(stderr, "setClassified %lx %u\n", addr, size);
    if constexpr (EnablePattern) {
      pattern_table.setClassified(addr, size);
    }
    cache.setClassified(addr, size, store_inst);
  }

  void printDesc(std::ostream& os) {
    os << "Configuration: pattern declassification table\n";
  }

  void printStats(std::ostream& os) {
    os << "pattern-hit " << stat_pattern_hit << "\n"
       << "cache-hit " << stat_cache_hit << "\n"
       << "upgrades " << stat_upgrades << "\n"
       << "discards " << stat_discards << "\n";
    pattern_table.printStats(os);
  }

  void dumpTaint(std::vector<uint8_t>&) {}

  void dump(std::ostream& os) {
    os << "==== CACHE ====\n";
    cache.dump(os);
    os << "\n=== DICT ====\n";
    pattern_table.dump(os);
  }

private:
  Cache& cache;
  PatternTable pattern_table;
  unsigned long stat_pattern_hit = 0;
  unsigned long stat_cache_hit = 0;
  unsigned long stat_upgrades = 0;
  unsigned long stat_discards = 0;
};
