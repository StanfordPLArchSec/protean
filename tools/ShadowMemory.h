#pragma once

#include "pin.H"

#include <array>

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
