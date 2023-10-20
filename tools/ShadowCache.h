#pragma once

#include "ShadowMemory.h"
#include "Util.h"

#include <optional>


template <size_t LineSize_, size_t NumLines_>
class ShadowCache {
public:
  static inline constexpr size_t LineSize = LineSize_;
  static_assert((LineSize & (LineSize - 1)) == 0, "");
  static inline constexpr size_t LineSizeBits = util::log2<LineSize>();
  static inline constexpr size_t NumLines = NumLines_;

  struct Line;
  using LRUList = std::list<Addr>;
  using LRUIt = LRUList::iterator;

  class EvictionQueue {
  public:
    using Queue = std::list<Addr>;
    using iterator = Queue::iterator;

    iterator insert(Addr addr, std::optional<Addr>& evicted_addr) {
      assert(queue.size() <= NumLines);
      if (queue.size() == NumLines) {
	evicted_addr = queue.front();
	queue.pop_front();
      }
      const auto it = queue.insert(queue.end(), addr);
      assert(queue.size() <= NumLines);
      return it;
    }

    void shift(iterator& it, size_t distance) {
      const Addr addr = *it;
      auto base_it = queue.erase(it);
      for (; base_it != queue.end() && distance > 0; ++base_it, --distance)
	;
      it = queue.insert(base_it, addr);
    }
    
  private:
    std::list<Addr> queue;
  };
  
  struct Line {
    Addr tag;
    std::array<bool, LineSize> bv;
    EvictionQueue::iterator it;
    unsigned long hits = 0;
    
    Line(Addr addr, EvictionQueue::iterator it): tag(getTag(addr)), it(it) {
      std::fill(bv.begin(), bv.end(), false);
    }
    static Addr getTag(Addr addr) { return addr / LineSize; }
    size_t getIndex(Addr addr) const { return addr & (LineSize - 1); }
    Addr baseaddr() const { return tag * LineSize; }
    void fillRange(Addr addr, unsigned size, bool value) {
      std::fill_n(bv.begin() + getIndex(addr), size, value);
    }
    bool check(Addr addr, unsigned size, bool value) const {
      const auto it = bv.begin() + getIndex(addr);
      return std::all_of(it, it + size, [value] (bool x) { return x == value; });
    }
    size_t popcnt() const {
      return std::count(bv.begin(), bv.end(), true);
    }
  };

  void shift(Line& line, size_t distance) {
    queue.shift(line.it, distance);
  }

  ShadowCache(): mem(std::nullopt), hit_file(nullptr) {}

  bool checkDeclassified(Addr addr, unsigned size) {
    constexpr size_t interval = 10000;
    static size_t counter = 0;
    ++counter;
    const auto log_hit = [&] (const Line& line, bool hit) {
      if (counter % interval == 0) {
	if (hit_file == nullptr) {
	  hit_file = fopen(getFilename("hits.out").c_str(), "w");
	}
	fprintf(hit_file, "%s %016lx %lu\n", hit ? "hit" : "miss", line.baseaddr(), line.hits);
      }
    };
    
    std::optional<Line>& line = mem[addr];
    if (!line)
      return false;
    if (!line->check(addr, size, true)) {
      log_hit(*line, false);
      return false;
    }
    line->hits++;
    log_hit(*line, true);
    // shift(*line, NumLines);
    return true;
  }

  void setDeclassified(Addr addr, unsigned size, Addr inst) {
    std::optional<Line>& line = mem[addr];
    if (!line) {
      std::optional<Addr> evicted_addr;
      const auto it = queue.insert(addr, evicted_addr);
      if (evicted_addr) {
	std::optional<Line>& evicted_line = mem[*evicted_addr];
	assert(evicted_line);
	evicted_line = std::nullopt;
      }
      line = Line(addr, it);
    }
    if (line->check(addr, size, true))
      return;
    line->fillRange(addr, size, true);
    // shift(*line, 64);
  }

  void setClassified(Addr addr, unsigned size, Addr inst) {
    std::optional<Line>& line = mem[addr];
    if (line) {
      line->fillRange(addr, size, false);
    }
  }

  void printDesc(std::ostream& os) const {
    os << "shadow cache with line size of " << LineSize << " and " << NumLines << " lines\n";
  }

  void printStats(std::ostream& os) const {}
  void dump(std::ostream& os) const {
    FILE *f = std::fopen(getFilename("lines.out").c_str(), "w");
    mem.for_each([&] (Addr addr, const std::optional<Line>& line) {
      if (line) {
	fprintf(f, "%016lx %s %lu\n", line->baseaddr(), bv_to_string8(line->bv).c_str(), line->hits);
      }
    });
    fclose(f);
  }

  ~ShadowCache() {
    if (hit_file)
      fclose(hit_file);
  }

private:
  ShadowMemory<std::optional<Line>, LineSizeBits, 12> mem;
  EvictionQueue queue;
  FILE *hit_file;
};
