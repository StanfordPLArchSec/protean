#include "pin.H"
#include <array>
#include <memory>
#include <vector>
#include <optional>
#include <list>

#include "ShadowMemory.h"
  
class ShadowDeclassificationTable {
public:
  ShadowDeclassificationTable():
    shadow_declassified(false),
    shadow_declassified_store(0),
    shadow_store_inst_classify_miss(0)
  {}
      
    
  bool checkDeclassified(ADDRINT eff_addr, unsigned eff_size) {
    bool *ptr = &shadow_declassified[eff_addr];
    const bool declassified = std::reduce(ptr, ptr + eff_size, true, std::logical_and<bool>());
    if (!declassified) {
      if (ADDRINT store = shadow_declassified_store[eff_addr]) {
	++shadow_store_inst_classify_miss[store];
	++stat_classify_misses;
      } else {
	++stat_cold_misses; 
      }
    } else {
      ++stat_hits;
    }
    return declassified;
  }
    
  void setDeclassified(ADDRINT eff_addr, unsigned eff_size) {
    bool *ptr = &shadow_declassified[eff_addr];
    std::fill(ptr, ptr + eff_size, true);
  }

  void setClassified(ADDRINT eff_addr, unsigned eff_size, ADDRINT store_inst) {
    bool *ptr = &shadow_declassified[eff_addr];
    std::fill(ptr, ptr + eff_size, false);
    ADDRINT *store_ptr = &shadow_declassified_store[eff_addr];
    std::fill(store_ptr, store_ptr + eff_size, store_inst);
  }

  void printStats(std::ostream& os) {
    os << "hits " << stat_hits << "\n"
       << "cold-misses " << stat_cold_misses << "\n"
       << "classify-misses " << stat_classify_misses << "\n";
    std::vector<std::pair<ADDRINT, unsigned long>> stores;
    shadow_store_inst_classify_miss.for_each([&] (ADDRINT store_inst, unsigned long count) {
      stores.emplace_back(store_inst, count);
    });
    std::sort(stores.begin(), stores.end(), [] (const auto& p1, const auto& p2) -> bool {
      return p2.second < p1.second;
    });
    for (unsigned i = 0; i < 10 && i < stores.size(); ++i) {
      os << "classify-store-hist-" << i << " " << stores[i].first << " " << stores[i].second << "\n";
    }
  }

  void printDesc(std::ostream& os) {
    os << "Configuration: shadow memory\n";
  }

  void dumpMem(std::vector<uint8_t>& mem) const {
    shadow_declassified.for_each([&mem] (ADDRINT addr, bool) {
      uint8_t byte = 0;
      PIN_SafeCopy(&byte, (const VOID *) addr, 1);
      mem.push_back(byte);
    });
  }

  void dumpTaint(std::vector<uint8_t>& mem) const {
    shadow_declassified.for_each_page([&mem] (ADDRINT, const auto& bv) {
      for (unsigned i = 0; i < bv.size(); i += 8) {
	uint8_t mask = 0;
	for (unsigned j = 0; j < 8; ++j) {
	  mask <<= 1;
	  if (bv[i + j])
	    mask |= 1;
	}
	mem.push_back(mask);
      }
    });
  }

private:
  ShadowMemory<bool, 0, 12> shadow_declassified;
  ShadowMemory<ADDRINT, 0, 12> shadow_declassified_store;
  ShadowMemory<unsigned long, 0, 12> shadow_store_inst_classify_miss;
  unsigned long stat_classify_misses = 0;
  unsigned long stat_cold_misses = 0;
  unsigned long stat_hits = 0;
#if 0
  ShadowMemory<int, 12, 0> shadow_declassified_pages(shadow_declassified_pages.ValueSize);
#endif
};


class FiniteLRUShadowDeclassificationTable {
public:
  using LRUList = std::list<ADDRINT>;
  using LRUIt = LRUList::iterator;
  
  FiniteLRUShadowDeclassificationTable(unsigned max_size): maxSize(max_size), shadow(std::nullopt) {}

  bool checkDeclassified(ADDRINT addr, unsigned size) {
    std::optional<LRUIt> *ptr = &shadow[addr];
    const bool declassified = std::all_of(ptr, ptr + size, [] (const auto& opt) -> bool {
      return static_cast<bool>(opt);
    });
    if (declassified) {
      // Mark all declassification bits as used.
      for (unsigned off = 0; off < size; ++off) {
	LRUIt& lru_it = *ptr[off];
	lru.erase(lru_it);
	lru.push_front(addr + off);
	lru_it = lru.begin();
      }
    }
    return declassified;
  }

  void setDeclassified(ADDRINT addr, unsigned size) {
    std::optional<LRUIt> *ptr = &shadow[addr];
    for (unsigned off = 0; off < size; ++off) {
      if (!ptr[off]) {
	// Evict if necessary
	if (lru.size() >= maxSize) {
	  const ADDRINT evict_addr = lru.back();
	  lru.pop_back();
	  shadow[evict_addr] = std::nullopt;
	}
	lru.push_front(addr + off);
	ptr[off] = lru.begin();
      }
    }
  }

  void setClassified(ADDRINT addr, unsigned size, ADDRINT) {
    std::optional<LRUIt> *ptr = &shadow[addr];
    for (unsigned off = 0; off < size; ++off) {
      if (std::optional<LRUIt>& lru_it = ptr[off]) {
	lru.erase(*lru_it);
	lru_it = std::nullopt;
      }
    }
  }

  void printDesc(std::ostream& os) {
    os << "finite-sized shadow declassification table, max_size=" << maxSize << "\n";
  }

  void printStats(std::ostream& os) {}

  void dumpMemory(FILE *f) {

  }
  
private:
  unsigned maxSize;
  ShadowMemory<std::optional<LRUIt>, 0, 12> shadow;
  LRUList lru;
};


class FiniteRandShadowDeclassificationTable {
public:
  using Addrs = std::vector<ADDRINT>;

  FiniteRandShadowDeclassificationTable(unsigned max_size): maxSize(max_size), shadow(nullptr) {
    addrs.resize(max_size);
    for (unsigned i = 0; i < max_size; ++i)
      free.push_back(&addrs[i]);
  }

  bool checkDeclassified(ADDRINT addr, unsigned size) {
    ADDRINT **ptr = &shadow[addr];
    return std::transform_reduce(ptr, ptr + size, true,
				 std::logical_and<bool>(),
				 [] (ADDRINT *x) -> bool { return x != nullptr; });
  }

  void setDeclassified(ADDRINT addr, unsigned size) {
    ADDRINT **ptr = &shadow[addr];
    for (unsigned off = 0; off < size; ++off) {
      ADDRINT* &val = ptr[off];
      if (val == nullptr) {
	assert(addrs.size() <= maxSize);
	if (free.empty()) {
	  // If the free list is empty, then we evict an existing address and push it to the free list.
	  const unsigned evicted_idx = std::rand() % addrs.size();
	  ADDRINT *evicted_addr_ptr = &addrs[evicted_idx];
	  free.push_back(evicted_addr_ptr);
	  shadow[*evicted_addr_ptr] = nullptr;
	}
	assert(!free.empty());
	val = free.back();
	*val = addr + off;
	free.pop_back();
      }
    }
  }

  void setClassified(ADDRINT addr, unsigned size, ADDRINT) {
    ADDRINT **ptr = &shadow[addr];
    for (unsigned off = 0; off < size; ++off) {
      ADDRINT* &val = ptr[off];
      if (val) {
	// Add to free list and zero.
	free.push_back(val);
	val = nullptr;
      }
    }
  }

  void printStats(std::ostream& os) {}

  void printDesc(std::ostream& os) {
    os << "finite random-evicted shadow declassification table with " << maxSize << " entries\n";
  }
  
private:
  unsigned maxSize;
  ShadowMemory<ADDRINT *, 0, 12> shadow;
  std::vector<ADDRINT *> free;
  std::vector<ADDRINT> addrs;
  unsigned nextFree = 0;
};
