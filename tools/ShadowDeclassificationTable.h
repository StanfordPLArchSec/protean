#include "pin.H"
#include <array>
#include <memory>
#include <vector>
#include <optional>
#include <list>

template <typename T>
constexpr T div_up(T numer, T denom) {
  return (numer + denom - 1) / denom;
}

template <typename T, unsigned ValueBits_, unsigned PageBits_>
class ShadowMemory {
public:
  static inline constexpr unsigned AddrBits = 48;
  
  static inline constexpr unsigned ValueBitLo = 0;
  static inline constexpr unsigned ValueBits = ValueBits_;
  static inline constexpr unsigned ValueBitHi = ValueBitLo + ValueBits;
  static inline constexpr unsigned ValueSize = 1 << ValueBits;
  
  static inline constexpr unsigned PageBitLo = ValueBitHi;
  static inline constexpr unsigned PageBits = PageBits_;
  static inline constexpr unsigned PageBitHi = PageBitLo + PageBits;
  static inline constexpr unsigned PageSize = 1 << PageBits;
  
  static inline constexpr unsigned TotalIndexBits = AddrBits - PageBitHi;
  
  static inline constexpr unsigned LowerIndexBitLo = PageBitHi;
  static inline constexpr unsigned LowerIndexBits = TotalIndexBits / 2;
  static inline constexpr unsigned LowerIndexBitHi = PageBitHi + LowerIndexBits;
  static inline constexpr unsigned LowerIndexSize = 1 << LowerIndexBits;
  
  static inline constexpr unsigned UpperIndexBitLo = LowerIndexBitHi;
  static inline constexpr unsigned UpperIndexBitHi = AddrBits;
  static inline constexpr unsigned UpperIndexBits = UpperIndexBitHi - UpperIndexBitLo;
  static inline constexpr unsigned UpperIndexSize = 1 << UpperIndexBits;

  using Page = std::array<T, 1 << PageBits>;
  
  ShadowMemory(const T& init) {
    std::fill(clean_page.begin(), clean_page.end(), init);
  }

  T& operator[](ADDRINT addr) {
    const ADDRINT upper_idx = (addr >> UpperIndexBitLo) & (UpperIndexSize - 1);
    const ADDRINT lower_idx = (addr >> LowerIndexBitLo) & (LowerIndexSize - 1);
    const ADDRINT page_idx = (addr >> PageBitLo) & (PageSize - 1);

    std::unique_ptr<LowerPageTable>& lower_ptable = mem[upper_idx];
    if (!lower_ptable)
      lower_ptable = std::make_unique<LowerPageTable>();

    std::unique_ptr<Page>& page = (*lower_ptable)[lower_idx];
    if (!page)
      page = std::make_unique<Page>(clean_page);

    return (*page)[page_idx];
  }

  void for_each(std::function<void (ADDRINT, const T&)> func) const {
    for (unsigned upper_idx = 0; upper_idx < (1 << UpperIndexBits); ++upper_idx) {
      if (const auto& lower_ptable = mem.at(upper_idx)) {
	for (unsigned lower_idx = 0; lower_idx < (1 << LowerIndexBits); ++lower_idx) {
	  if (const auto& page = (*lower_ptable)[lower_idx]) {
	    for (unsigned off = 0; off < (1 << PageBits); ++off) {
	      const ADDRINT addr =
		(upper_idx << UpperIndexBitLo) |
		(lower_idx << LowerIndexBitLo) |
		(off << ValueBitLo);
	      func(addr, (*page)[off]);
	    }
	  }
	}
      }
    }
  }

  void for_each_page(std::function<void (ADDRINT, const Page&)> func) const {
    for (unsigned upper_idx = 0; upper_idx < (1 << UpperIndexBits); ++upper_idx) {
      if (const auto& lower_ptable = mem.at(upper_idx)) {
	for (unsigned lower_idx = 0; lower_idx < (1 << LowerIndexBits); ++lower_idx) {
	  if (const auto& page = (*lower_ptable)[lower_idx]) {
	    const ADDRINT addr = (upper_idx << UpperIndexBitLo) | (lower_idx << LowerIndexBitLo);
	    func(addr, *page);
	  }
	}
      }
    }
  }
  
private:
  Page clean_page;
  using LowerPageTable = std::array<std::unique_ptr<Page>, 1 << LowerIndexBits>;
  using UpperPageTable = std::array<std::unique_ptr<LowerPageTable>, 1 << UpperIndexBits>;
  
  UpperPageTable mem;
};

  
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
