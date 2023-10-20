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
  
  struct Line {
    Addr tag;
    std::array<bool, LineSize> bv;
    LRUIt lru_it;

    Line(Addr addr, LRUIt lru_it): tag(getTag(addr)), lru_it(lru_it) {
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
  };


  ShadowCache(): mem(std::nullopt) {}

  void markUsed(Line& line) {
    lru.erase(line.lru_it);
    line.lru_it = lru.insert(lru.end(), line.baseaddr());
  }

  bool checkDeclassified(Addr addr, unsigned size) {
    std::optional<Line>& line = mem[addr];
    if (!line)
      return false;
    if (!line->check(addr, size, true))
      return false;
    markUsed(*line);
    return true;
  }

  void setDeclassified(Addr addr, unsigned size, Addr inst) {
    std::optional<Line>& line = mem[addr];
    if (!line) {
      if (lru.size() == NumLines) {
	const Addr evicted_baseaddr = lru.front();
	lru.pop_front();
	std::optional<Line>& evicted_line = mem[evicted_baseaddr];
	assert(evicted_line);
	evicted_line = std::nullopt;
      }
      const auto lru_it = lru.insert(lru.end(), addr);
      line = Line(addr, lru_it);
    }
    assert(lru.size() <= NumLines);
    line->fillRange(addr, size, true);
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
  void dump(std::ostream& os) const {}

private:
  ShadowMemory<std::optional<Line>, LineSizeBits, 12> mem;
  LRUList lru;
};
