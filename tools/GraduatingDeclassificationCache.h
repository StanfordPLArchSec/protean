#pragma once

#include <vector>
#include <cassert>
#include <memory>

#include "Util.h"

class GraduatingDeclassificationCache {
private:

  struct GradTable {
    size_t quorum;
    size_t line_size;
    std::vector<size_t> entries;
    unsigned long stat_zero_dec = 0;
    
    GradTable(size_t line_size, size_t num_entries, size_t quorum): quorum(quorum), line_size(line_size), entries(num_entries, 0) {
      assert((line_size & (line_size - 1)) == 0);
      assert((num_entries & (num_entries - 1)) == 0);
    }

    size_t size() const { return entries.size(); }

  private:
    size_t getIndex(Addr addr) const {
      return (addr / line_size) & (size() - 1);
    }

    size_t& getRef(Addr addr) {
      return entries[getIndex(addr)];
    }

  public:

    // Returns whether the line reached quorum and should be upgraded.
    bool inc(Addr addr) {
      size_t& counter = getRef(addr);
      assert(counter < quorum);
      ++counter;
      if (counter == quorum) {
	counter = 0;
	return true;
      }
      return false;
    }

    void dec(Addr addr) {
      size_t& counter = getRef(addr);
      if (counter == 0) {
	++stat_zero_dec;
      } else {
	--counter;
      }
    }

    void dump(std::ostream& os) const {
      for (size_t entry : entries) {
	os << entry << "\n";
      }
    }
  };


  using Tag = Addr;

  struct Line {
    bool valid;
    Tag tag;
    bool used;
    std::vector<bool> bv;

    Line(size_t line_size): valid(false), tag(0), used(false), bv(line_size, false) {}

    bool contains(Addr addr) const {
      return valid && addr / size() == tag;
    }
    size_t getIndex(Addr addr) const {
      assert(contains(addr));
      return addr & (size() - 1);
    }
    size_t size() const { return bv.size(); }
    Addr baseaddr() const { return tag * size(); }
    Addr endaddr() const { return (tag + 1) * size(); }
    size_t popcnt() const { return ::popcnt(bv); }
    void invalidate() { valid = false; }
    bool allSet(Addr addr, unsigned size) const {
      assert(contains(addr));
      const size_t idx = getIndex(addr);
      return std::reduce(bv.begin() + idx, bv.begin() + idx + size, true, std::logical_and<bool>());
    }
    bool allReset(Addr addr, unsigned size) const {
      assert(contains(addr));
      const size_t idx = getIndex(addr);
      return !std::reduce(bv.begin() + idx, bv.begin() + idx + size, false, std::logical_or<bool>());
    }
    size_t setRange(Addr addr, unsigned size, bool value) {
      std::fill_n(bv.begin() + getIndex(addr), size, value);
      return popcnt();
    }
    template <typename InputIt>
    void copyRange(Addr addr, unsigned size, InputIt it) {
      std::copy_n(it, size, bv.begin() + getIndex(addr));
    }

    template <class BV>
    void init(Addr addr, const BV& new_bv) {
      valid = true;
      tag = addr / size();
      used = false;
      assert(bv.size() >= new_bv.size());
      std::fill(bv.begin(), bv.end(), false);
      copyRange(addr, new_bv.size(), new_bv.begin());
    }

    void dump(std::ostream& os) const {
      if (!valid) {
	os << "(invalid)";
      } else {
	os << std::setfill('0') << std::setw(16) << baseaddr() << " " << (used ? "used" : "unused") << " " << bv_to_string1(bv);
      }
    }
  };

  struct Row {
    std::vector<Line> lines;

    Row(size_t num_cols, const Line& line): lines(num_cols, line) {}

    bool contains(Addr addr) const {
      return std::any_of(lines.begin(), lines.end(), [addr] (const Line& line) { return line.contains(addr); });
    }

    Line *tryGetLine(Addr addr) {
      for (Line& line : lines)
	if (line.contains(addr))
	  return &line;
      return nullptr;
    }

  private:
    Line& evictLine() {
      for (Line& line : lines)
	if (!line.valid)
	  return line;
      
      for (Line& line : lines)
	if (!line.used)
	  return line;

      return lines[std::rand() % lines.size()];
    }

  public:

    template <class Range>
    Line allocate(Addr addr, const Range& bv) {
      assert(!contains(addr));

      Line& line = evictLine();
      const auto evicted_line = line;
      line.init(addr, bv);
      return evicted_line;
    }

    void dump(std::ostream& os) const {
      for (const Line& line : lines) {
	line.dump(os);
	os << "\n";
      }
    }
  };

  struct Cache {
    std::string name;
    size_t line_size;
    std::vector<Row> rows;
    GradTable *grad_table;
    unsigned long stat_evictions = 0;
    unsigned long stat_evictions_used = 0;
    unsigned long stat_graduations = 0;
    unsigned long stat_hits = 0;

    Cache(const std::string& name, size_t line_size, size_t num_rows, const Row& row):
      name(name), line_size(line_size), rows(num_rows, row), grad_table(nullptr) {}

    size_t size() const { return rows.size(); }
    size_t getIndex(Addr addr) const {
      return (addr / line_size) & (size() - 1);
    }
    const Row& getRow(Addr addr) const { return rows[getIndex(addr)]; }
    Row& getRow(Addr addr) { return rows[getIndex(addr)]; }
    bool contains(Addr addr) const { return getRow(addr).contains(addr); }
    Line *tryGetLine(Addr addr) { return getRow(addr).tryGetLine(addr); }

    bool checkDeclassified(Addr addr, unsigned size, std::optional<Line> *grad_line) {
      assert(size <= line_size);
      Line *line = tryGetLine(addr);
      if (!line)
	return false;
      assert(line->valid);
      assert(line->tag == addr / line->size());

      // Check if hit.
      if (!line->allSet(addr, size))
	return false; // Was a miss.

      // Check if used.
      // If not, then set used and update grad table.
      if (!line->used) {
	line->used = true;
	if (grad_table && grad_table->inc(addr)) {
	  // We hit quorum so we can graduate!
	  *grad_line = *line;
	  line->invalidate();
	  ++stat_graduations;
	}
      }

      ++stat_hits;
      
      return true;
    }


    // Returns whether we handled the setDeclassified() request.
    // NOTE: We never allocate a line here.
    bool setDeclassified(Addr addr, unsigned size) {
      assert(size <= line_size);
      Line *line = tryGetLine(addr);
      if (!line)
	return false;
      line->setRange(addr, size, true);
      return true;
    }

    bool setClassified(Addr addr, unsigned size) {
      assert(size <= line_size);
      Line *line = tryGetLine(addr);
      if (!line)
	return false;
      const size_t pop = line->setRange(addr, size, false);
      if (pop == 0) {
	// Deallocate the line entirely.
	line->invalidate();
	if (grad_table)
	  grad_table->dec(addr);
      }
      return true;
    }
    
    std::optional<Line> takeLine(Addr addr) {
      Line *line = tryGetLine(addr);
      if (!line)
	return std::nullopt;
      auto stolen_line = *line;
      line->invalidate();
      return stolen_line;
    }

    template <class Range>
    Line allocate(Addr addr, const Range& bv) {
      const Line evicted_line = getRow(addr).allocate(addr, bv);
      if (evicted_line.valid) {
	++stat_evictions;
	if (evicted_line.used)
	  ++stat_evictions_used;
	if (grad_table)
	  grad_table->dec(addr);
      }
      return evicted_line;
    }

    void printStats(std::ostream& os) const {
      os << name << ".evictions " << stat_evictions << "\n";
      os << name << ".evictions_used " << stat_evictions_used << "\n";
      os << name << ".graduations " << stat_graduations << "\n";
      os << name << ".hits " << stat_hits << "\n";
    }

    void dump(std::ostream& os) const {
      for (const Row& row : rows)
	row.dump(os);
    }
  };


private:

  size_t holdCount(Addr addr) const {
    return std::count_if(caches.begin(), caches.end(), [addr] (const Cache& cache) {
      return cache.contains(addr);
    });
  }

  template <class BV>
  void tryGraduateData(Addr addr, const BV& bv) {
    assert(holdCount(addr) <= 1);

    for (Cache& cache : caches) {
      if (Line *line = cache.tryGetLine(addr)) {
	line->copyRange(addr, bv.size(), bv.begin());
	break;
      }
    }
  }

  // Must be called before anything else to ensure
  // we uphold our invariant.
  void tryGraduateLine(Addr addr) {
    // First, find highest table, if any, to contain this line.
    Line *upper_line = nullptr;
    auto cache_it = caches.rbegin();
    for (; !upper_line && cache_it != caches.rend(); ++cache_it) {
      upper_line = cache_it->tryGetLine(addr);
    }
    if (!upper_line) {
      assert(holdCount(addr) == 0);
      return;
    }

    // Second, find lower table, if any.
    Line *lower_line = nullptr;
    for (; !lower_line && cache_it != caches.rend(); ++cache_it) {
      lower_line = cache_it->tryGetLine(addr);
    }
    if (!lower_line) {
      assert(holdCount(addr) == 1);
      return;
    }

    assert(lower_line != upper_line && upper_line->baseaddr() <= lower_line->baseaddr() && lower_line->endaddr() <= upper_line->endaddr());
    assert(upper_line->allReset(lower_line->baseaddr(), lower_line->size()));
    upper_line->copyRange(lower_line->baseaddr(), lower_line->size(), lower_line->bv.begin());
    lower_line->invalidate();

    if (holdCount(addr) > 1)
      tryGraduateLine(addr);
  }

public:
  // TODO: Do graduation of accessed line *before* any accesses just to simplify the implementation for now.
  // This means in the body of checkDeclassified(), setDeclassified(), and setClassified(), we may assume that
  // any line we're handling does not belong to a higher level. 
  
  bool checkDeclassified(Addr addr, unsigned size) {
    if (size > 1) {
      return checkDeclassified(addr, size / 2) && checkDeclassified(addr + size / 2, size / 2);
    }
    
    tryGraduateLine(addr);
    assert(holdCount(addr) <= 1);

    bool is_declassified = false;
    for (size_t i = 0; i < caches.size(); ++i) {
      std::optional<Line> grad_line;
      if (caches[i].checkDeclassified(addr, size, &grad_line)) {
	if (grad_line) {
	  // Graduate to upper table.
	  assert(i + 1 < caches.size());
	  Cache& upper_cache = caches[i + 1];
	  assert(holdCount(addr) == 0);
	  const auto evicted_line = upper_cache.allocate(grad_line->baseaddr(), grad_line->bv);
	  assert(holdCount(addr) == 1);
#if 0
	  if (evicted_line)
	    tryGraduateData(evicted_line->baseaddr(), evicted_line->bv);
#endif
	}
	return true;
      }
      assert(!grad_line);
      assert(holdCount(addr) <= 1);
    }
    return false;
  }

  void setDeclassified(Addr addr, unsigned size) {
    if (size > 1) {
      setDeclassified(addr, size / 2);
      setDeclassified(addr + size / 2, size / 2);
      return;
    }
    
    tryGraduateLine(addr);
    assert(holdCount(addr) <= 1);

    for (Cache& cache : caches) {
      if (cache.setDeclassified(addr, size)) {
	assert(holdCount(addr) <= 1);
	return;
      }
    }

    assert(holdCount(addr) == 0);

    // Allocate a new line in the lowest level cache.
    std::vector<bool> bv(size, true);
    const auto evicted_line = caches.front().allocate(addr, bv);
    assert(holdCount(addr) == 1);
#warning EViction handlign is diables
#if 0
    if (evicted_line)
      tryGraduateData(evicted_line->baseaddr(), evicted_line->bv);
#endif
  }

  void setDeclassified(Addr addr, unsigned size, bool allocate) { setDeclassified(addr, size); }


  void setClassified(Addr addr, unsigned size, Addr store_inst) {
    if (size > 1) {
      setClassified(addr, size / 2, store_inst);
      setClassified(addr + size / 2, size / 2, store_inst);
      return;
    }

    tryGraduateLine(addr);
    assert(holdCount(addr) <= 1);

    for (Cache& cache : caches) {
      if (cache.setClassified(addr, size))
	break;
    }
  }

  struct CacheSpec {
    size_t line_size;
    size_t table_size;
    size_t associativity;

    size_t table_cols() const { return associativity; }
    size_t table_rows() const { return table_size / associativity; }
    void validate() const {
      assert((line_size & (line_size - 1)) == 0);
      assert((table_rows() & (table_rows() - 1)) == 0);
      assert(table_size % table_cols() == 0);
    }
  };

  struct GradSpec {
    size_t size;
    size_t quorum;
    size_t line_size;

    void validate() const {
      assert((size & (size - 1)) == 0);
    }
  };

  GraduatingDeclassificationCache(const std::string& name, std::initializer_list<CacheSpec> cache_specs, std::initializer_list<GradSpec> grad_specs):
    name(name)
  {
    assert(cache_specs.size() == grad_specs.size() + 1);
    for (const CacheSpec& cache_spec : cache_specs) {
      cache_spec.validate();
      const Line line(cache_spec.line_size);
      const Row row(cache_spec.table_cols(), line);
      caches.emplace_back(name + ".cache" + std::to_string(caches.size()), cache_spec.line_size, cache_spec.table_rows(), row);
    }
    for (const GradSpec& grad_spec : grad_specs) {
      grad_spec.validate();
      grads.emplace_back(grad_spec.line_size, grad_spec.size, grad_spec.quorum);
    }
    for (size_t i = 0; i < grads.size(); ++i) {
      caches[i].grad_table = &grads[i];
    }
  }

  void printDesc(std::ostream& os) {
    os << name << ": graduating declassification cache\n";
  }

  void printStats(std::ostream& os) {
    for (const Cache& cache : caches) {
      cache.printStats(os);
    }
  }
  
  void dump(std::ostream& os) {
    for (size_t i = 0; i < caches.size(); ++i) {
      std::ofstream f(getFilename(name + ".cache" + std::to_string(i)));
      caches[i].dump(f);
    }
    for (size_t i = 0; i < grads.size(); ++i) {
      std::ofstream f(getFilename(name + ".grad" + std::to_string(i)));
      grads[i].dump(f);
    }
  }

private:
  std::string name;
  std::vector<Cache> caches;
  std::vector<GradTable> grads;
  
};
