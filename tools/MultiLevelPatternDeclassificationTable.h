#pragma once

#include <array>
#include <vector>
#include <initializer_list>
#include <cassert>

#include "Util.h"


template <size_t InChunkSize, size_t MaxPatternLength>
class MultiLevelPatternDeclassificationTable {
  static_assert((InChunkSize & (InChunkSize - 1)) == 0, "");
  static_assert(InChunkSize >= MaxPatternLength, "");
private:
  using Tick = unsigned;
  
  struct Line {
    bool valid;
    size_t chunk_size;
    Addr tag;
    std::vector<bool> pat;
    std::vector<bool> bv;

    // Eviction-relevant information.
    Tick *tick = nullptr;
    Tick used = 0;

    Line(size_t chunk_size, size_t line_size, Tick& tick): valid(false), chunk_size(chunk_size), bv(line_size), tick(&tick) {}

    Addr baseaddr() const { return tag * lineSize() * chunk_size; }
    size_t lineSize() const { return bv.size(); }
    bool contains(Addr addr) const {
      return valid && addr / chunk_size / lineSize() == tag;
    }
    bool allSet() const {
      assert(valid);
      return std::reduce(bv.begin(), bv.end(), true, std::logical_and<bool>());
    }
    bool allSet(size_t idx, size_t size) const {
      return std::reduce(bv.begin() + idx, bv.begin() + idx + size, true, std::logical_and<bool>());
    }
    
    bool allReset() const {
      assert(valid);
      return !std::reduce(bv.begin(), bv.end(), false, std::logical_or<bool>());
    }

    bool check(Addr addr, unsigned size, bool value) {
      assert(contains(addr));
      const size_t idx = (addr / chunk_size) & (lineSize() - 1);
      if (!bv[idx])
	return false;
      for (unsigned i = 0; i < size; ++i) {
	const unsigned patidx = (addr + i - baseaddr()) % pat.size();
	if (pat[patidx] != value)
	  return false;
      }
      markUsed(); // TODO: Might want to selectively mark used.
      return true;
    }

    bool set(Addr addr, unsigned size, bool value) {
      if (check(addr, size, value)) {
	return true;
      }

      const size_t idx = (addr / chunk_size) & (lineSize() - 1);
      bv[idx] = false;
      if (allReset())
	valid = false;
      return false;
    }

    size_t popCnt() const {
      return std::count(bv.begin(), bv.end(), true);
    }

    void markUsed() {
      used = ++*tick;
    }

  private:
    // Align the pattern discovered at the given address
    // to our base address.
    void alignPattern(Addr addr, std::vector<bool>& pat) const {
#ifndef NDEBUG
      const std::vector<bool> orig_pat = pat;
#endif
      const size_t rshift = (addr - baseaddr()) % pat.size();
      const size_t lshift = rshift == 0 ? 0 : pat.size() - rshift;
      std::rotate(pat.begin(), pat.begin() + lshift, pat.end());

      // Check to make sure we got this right.
#ifndef NDEBUG
      std::vector<bool> buf(addr - baseaddr() + pat.size());
      for (size_t i = 0; i < buf.size(); ++i)
	buf[i] = pat[i % pat.size()];
      const bool eq = std::equal(&buf[addr - baseaddr()], &buf[addr - baseaddr() + pat.size()],
				 orig_pat.begin(), orig_pat.end());
      if (!eq) {
	fprintf(stderr, "alignPattern broken!\n");
	abort();
      }
	
#endif
    }

  public:
    bool claim(Addr addr, std::vector<bool> their_pat) {
      assert((addr & (chunk_size - 1)) == 0);
      if (their_pat.size() != pat.size())
	return false;

      // Rotate pattern so it aligns with base address.
      alignPattern(addr, their_pat);
      if (!std::equal(pat.begin(), pat.end(), their_pat.begin()))
	return false;
      
      // We matched the pattern!
      const size_t line_idx = (addr / chunk_size) & (lineSize() - 1);
      bv[line_idx] = true;
      
      markUsed();
      
      return true;
    }

    void init(Addr addr, const std::vector<bool>& their_pat) {
      assert((addr & (chunk_size - 1)) == 0);
      valid = true;
      tag = addr / chunk_size / lineSize();
      pat = their_pat;
      alignPattern(addr, pat);
      std::fill(bv.begin(), bv.end(), false);
      const size_t idx = (addr / chunk_size) & (lineSize() - 1);
      bv[idx] = true;
      markUsed();
    }

    void dump(std::ostream& os) const {
      os << std::setfill('0') << std::setw(16) << tag << " " << bv_to_string1(pat) << " " << bv_to_string1(bv);
    }
  };

  struct Row {
    const size_t chunk_size;
    const size_t line_size;
    std::vector<Line> lines;
    unsigned long stat_evictions = 0;
    unsigned long stat_eviction_bits = 0;
    unsigned long stat_eviction_spans = 0;
    unsigned long stat_upgrades = 0;

    // Eviction information
    Tick tick = 0; // NOTE: This is shared between lines.

    Row(size_t num_cols, size_t chunk_size, size_t line_size):
      chunk_size(chunk_size), line_size(line_size), lines(num_cols, Line(chunk_size, line_size, tick)) {}

    Line *getLine(Addr addr) {
      for (auto& line : lines)
	if (line.contains(addr)) 
	  return &line;
      return nullptr;
    }

  private:
    static size_t statComputeOneMaxSpan(const Line& line) {
      for (auto span = line.lineSize(); span > 0; span /= 2)
        for (size_t i = 0; i < line.lineSize(); i += span)
          if (line.allSet(i, span))
            return span;
      __builtin_unreachable();
    }

    size_t statComputeMaxSpan() const {
      return std::transform_reduce(lines.begin(), lines.end(), static_cast<size_t>(0),
                                   [] (size_t a, size_t b) { return std::max(a, b); },
                                   &Row::statComputeOneMaxSpan);
    }
      
  public:

    Line& evictLine() {
      // First, look for invalid lines.
      for (Line& line : lines)
	if (!line.valid)
	  return line;

      // Next, look for lines that have all their bits set.
      for (Line& line : lines) {
	if (line.allSet()) {
	  ++stat_upgrades;
	  return line;
	}
      }

      // Otherwise, pick a random one.
      // TODO: Use a more intelligent algorithm, like LRU+popcnt.

      const auto popcnt_metric = [] (const Line& line) -> int {
	return line.popCnt() * popcnt(line.pat) / line.pat.size();
      };
      const auto lru_metric = [] (const Line& line) -> int {
	return line.used;
      };
      const auto metric = [&] (const Line& line) -> int {
	return lru_metric(line) + 4 * popcnt_metric(line);
      };
      const auto evicted_it = std::min_element(lines.begin(), lines.end(), [&] (const Line& a, const Line& b) -> bool {
	return metric(a) < metric(b);
      });
      assert(evicted_it != lines.end());
      Line& evicted_line = *evicted_it;

      // Stats
      {
	++stat_evictions;
	stat_eviction_bits += evicted_line.popCnt() * popcnt(evicted_line.pat) / evicted_line.pat.size();
        stat_eviction_spans += log2(statComputeMaxSpan());
      }
	
      return evicted_line;
    }

    bool check(Addr addr, unsigned size, bool value) {
      for (Line& line : lines)
	if (line.contains(addr))
	  return line.check(addr, size, value);
      return false;
    }

    bool set(Addr addr, unsigned size, bool value) {
      for (Line& line : lines)
	if (line.contains(addr))
	  return line.set(addr, size, value);
      return false;
    }

    bool claim(Addr addr, const std::vector<bool>& pat, std::optional<Line>& evicted) {
      Line *line = getLine(addr);
      if (line)
	return line->claim(addr, pat);
      
      line = &evictLine();
      evicted = *line;
      line->init(addr, pat);
      return true;
    }

    void dump(std::ostream& os) const {
      for (const Line& line : lines) {
	line.dump(os);
	os << "\n";
      }
    }
  };
  
  struct Table {
    size_t chunk_size;
    size_t line_size;
    std::vector<Row> rows;

    Table(size_t num_rows, size_t num_cols, size_t chunk_size, size_t line_size):
      chunk_size(chunk_size), line_size(line_size), rows(num_rows, Row(num_cols, chunk_size, line_size)) {}

    Row& getRow(Addr addr) {
      const Addr tag = addr / chunk_size / line_size;
      const size_t row_idx = tag & (rows.size() - 1);
      return rows[row_idx];
    }

    bool check(Addr addr, unsigned size, bool value) {
      return getRow(addr).check(addr, size, value);
    }

    bool set(Addr addr, unsigned size, bool value) {
      return getRow(addr).set(addr, size, value);
    }

    bool claim(Addr addr, const std::vector<bool>& pat, std::optional<Line>& evicted) {
      return getRow(addr).claim(addr, pat, evicted);
    }

  private:
    unsigned long sumRowStat(unsigned long Row::*mem_ptr) const {
      return std::transform_reduce(rows.begin(), rows.end(), 0UL, std::plus<unsigned long>(), std::mem_fn(mem_ptr));
    }
  public:
    void printStats(std::ostream& os, const std::string& name) const {
      os << name << "-evictions " << sumRowStat(&Row::stat_evictions) << "\n";
      os << name << "-eviction-bits " << sumRowStat(&Row::stat_eviction_bits) << "\n";
      os << name << "-eviction-spans " << sumRowStat(&Row::stat_eviction_spans) << "\n";
      os << name << "-upgrades " << sumRowStat(&Row::stat_upgrades) << "\n";
    }
    
    void dump(std::ostream& os) const {
      for (const Row& row : rows) {
	row.dump(os);
      }
    }
  };

public:
  struct TableSpec {
    size_t line_size;
    size_t associativity;
    size_t entries;
  };

  MultiLevelPatternDeclassificationTable(const std::string& name, std::initializer_list<TableSpec> specs):
    name(name)
  {
    size_t chunk_size = InChunkSize;
    for (const TableSpec& spec : specs) {
      assert((spec.line_size & (spec.line_size - 1)) == 0);
      assert(spec.entries % spec.associativity == 0);
      const size_t num_rows = spec.entries / spec.associativity;
      assert((num_rows & (num_rows - 1)) == 0);
      tables.emplace_back(num_rows, spec.associativity, chunk_size, spec.line_size);
      chunk_size *= spec.line_size;
    }
  }

  bool checkDeclassified(Addr addr, unsigned size) {
    // fprintf(stderr, "checkDeclassified %016lx %u\n", addr, size);
    for (Table& table : tables)
      if (table.check(addr, size, true))
	return true;
    return false;
  }

  bool setDeclassified(Addr addr, unsigned size) {
    // fprintf(stderr, "setDeclassified %016lx %u\n", addr, size);
    for (Table& table : tables) {
      if (table.set(addr, size, true)) {
	return true;
      }
    }
    return false;
  }

  bool setDeclassified(Addr addr, unsigned size, bool& downgrade, Addr& downgrade_addr, std::array<bool, InChunkSize>& downgrade_bv) {
    downgrade = false;
    return setDeclassified(addr, size);
  }

  bool setClassified(Addr addr, unsigned size) {
    // fprintf(stderr, "setClassified %016lx %u\n", addr, size);
    for (Table& table : tables) {
      if (table.set(addr, size, false)) {
	assert(!checkDeclassified(addr, size));
	return true;
      }
    }
    return false;
  }

  bool setClassified(Addr addr, unsigned size, Addr store_inst) { return setClassified(addr, size); }

  bool setClassified(Addr addr, unsigned size, Addr store_inst, bool& downgrade, Addr& downgrade_addr, std::array<bool, InChunkSize>& downgrade_bv) {
    downgrade = false;
    return setClassified(addr, size);
  }
  
  bool claimLine(Addr addr, const std::array<bool, InChunkSize>& bv) {
    // Is this a repeating pattern?
    std::vector<bool> pat;
    if (!hasPattern(bv.begin(), bv.end(), std::back_inserter(pat), MaxPatternLength))
      return false;

    // This has a repeating pattern.
    // Therefore, we can claim it.
    // TODO: Don't evict the last table entry!
    for (auto table_it = tables.begin(); table_it != tables.end(); ++table_it) {
      Table& table = *table_it;
      assert(!pat.empty());
      std::optional<Line> evicted_line;
      const bool claimed = table.claim(addr, pat, evicted_line);
      assert(table_it == tables.begin() || claimed);
      if (!claimed)
	return false;

      // Break if evicted line cannot be promoted.
      if (!(evicted_line && evicted_line->valid && evicted_line->allSet()))
	break;

      // Note that the patterns should be rotations of each other but
      // are not necessarily identical.
      // assert(evicted_line.pat == pat);
      pat = std::move(evicted_line->pat);
      addr = evicted_line->baseaddr();
    }

    return true;
  }

  void printDesc(std::ostream& os) {
    os << name << ": multi-level pattern declassification table\n";
  }

  void printStats(std::ostream& os) const {
    for (unsigned table_idx = 0; table_idx < tables.size(); ++table_idx) {
      const Table& table = tables[table_idx];
      const std::string table_name = name + "-t" + std::to_string(table_idx);
      table.printStats(os, table_name);
    }
  }

  void dump(std::ostream& os) {
    for (unsigned table_idx = 0; table_idx < tables.size(); ++table_idx) {
      std::stringstream ss;
      ss << "t" << table_idx << ".txt";
      std::ofstream ofs(getFilename(ss.str()));
      tables[table_idx].dump(ofs);
    }
  }
  
private:
  std::string name;
  std::vector<Table> tables;
  unsigned stat_promoted = 0;
};
