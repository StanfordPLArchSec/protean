/*
 * Copyright (C) 2007-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 *  This is an example of the PIN tool that demonstrates some basic PIN APIs 
 *  and could serve as the starting point for developing your first PIN tool
 */

#include "pin.H"
#include <iostream>
#include <fstream>
#include <set>
#include <vector>
#include <array>
#include <numeric>
#include <algorithm>
#include <memory>
#include <cinttypes>
#include "Declassify.h"
using std::cerr;
using std::endl;
using std::string;


KNOB<string> OutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "specify file name for output");
FILE *trace;
unsigned long staticAccessCount = 0;

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
  
private:
  using Page = std::array<T, 1 << PageBits>;
  Page clean_page;
  using LowerPageTable = std::array<std::unique_ptr<Page>, 1 << LowerIndexBits>;
  using UpperPageTable = std::array<std::unique_ptr<LowerPageTable>, 1 << UpperIndexBits>;
  
  UpperPageTable mem;
};

static ShadowMemory<bool, 0, 12> shadow_declassified(false);
static ShadowMemory<ADDRINT, 0, 12> shadow_classified_store(0);
static ShadowMemory<unsigned long, 0, 12> shadow_store_inst_classify_miss(0);
static ShadowMemory<bool, 0, 12> shadow_cold_miss(false);
static ShadowMemory<int, 12, 0> shadow_declassified_pages(shadow_declassified_pages.ValueSize);
static unsigned long decltab_hits = 0;
static unsigned long decltab_misses_cold = 0;
static unsigned long decltab_hits_page = 0;

static void RecordDeclassifiedLoad(ADDRINT eff_addr, UINT32 eff_size) {
  // Check if aligned
  if ((eff_addr & (eff_size - 1)) != 0) {
    // Unaligned: split in two
    RecordDeclassifiedLoad(eff_addr, eff_size / 2);
    RecordDeclassifiedLoad(eff_addr + eff_size / 2, eff_size / 2);
    return;
  }
  
  bool *shadow_declassified_ptr = &shadow_declassified[eff_addr];
  const unsigned num_classified_bytes =
    std::count(shadow_declassified_ptr, shadow_declassified_ptr + eff_size, false);
  const bool is_declassified = (num_classified_bytes == 0);
  if (is_declassified) {
    ++decltab_hits;
    if (shadow_declassified_pages[eff_addr] == 0)
      ++decltab_hits_page;
  } else {
    const ADDRINT *stores = &shadow_classified_store[eff_addr];
    UINT32 i;
    for (i = 0; i < eff_size; ++i) {
      ADDRINT store = stores[i];
      if (!shadow_declassified_ptr[i] && store) {
	++shadow_store_inst_classify_miss[store];
	break;
      }
    }
    if (i == eff_size) {
      bool *is_cold_miss = &shadow_cold_miss[eff_addr];
      if (std::reduce(is_cold_miss, is_cold_miss + eff_size, true, std::logical_and<bool>())) {
	PIN_ERROR("double cold miss");
      }
      std::fill_n(is_cold_miss, eff_size, true);
      ++decltab_misses_cold;
    }
    shadow_declassified_pages[eff_addr] -= num_classified_bytes;
    std::fill_n(shadow_declassified_ptr, eff_size, true);
  }
}

static void RecordClassifiedStore(ADDRINT st_inst, ADDRINT eff_addr, ADDRINT eff_size) {
  // Check if aligned
  if ((eff_addr & (eff_size - 1)) != 0) {
    RecordClassifiedStore(st_inst, eff_addr, eff_size / 2);
    RecordClassifiedStore(st_inst, eff_addr + eff_size / 2, eff_size / 2);
    return;
  }

  bool *ptr = &shadow_declassified[eff_addr];
  const unsigned num_declassified_bytes = std::count(ptr, ptr + eff_size, true);
  std::fill_n(ptr, eff_size, false);
  std::fill_n(&shadow_classified_store[eff_addr], eff_size, st_inst);
  shadow_declassified_pages[eff_addr] += num_declassified_bytes;
}

static void RecordDeclassifiedStore(ADDRINT eff_addr, ADDRINT eff_size) {
  // Check if aligned
  if ((eff_addr & (eff_size - 1)) != 0) {
    RecordDeclassifiedStore(eff_addr, eff_size / 2);
    RecordDeclassifiedStore(eff_addr + eff_size / 2, eff_size / 2);
    return;
  }
  
  bool *ptr = &shadow_declassified[eff_addr];
  const unsigned num_classified_bytes = std::count(ptr, ptr + eff_size, false);
  if (num_classified_bytes > 0) {
    std::fill_n(ptr, eff_size, true);
    shadow_declassified_pages[eff_addr] -= num_classified_bytes;
  }
}


static void Instruction(INS ins, void *v) {
  uint32_t memOperands = INS_MemoryOperandCount(ins);

  if (INS_HasScatteredMemoryAccess(ins))
    return;

  char buf[16];
  bool declassified = false;
  PIN_SafeCopy(buf, (void *) INS_Address(ins), INS_Size(ins));
  if (buf[0] == 0x36 /*SS*/ || (buf[0] == 0x66 && buf[1] == 0x36)) {
    declassified = true;
  }

  for (uint32_t memOp = 0; memOp < memOperands; ++memOp) {
    const UINT32 eff_size = INS_MemoryOperandSize(ins, memOp);
    if (INS_MemoryOperandIsRead(ins, memOp) && declassified) {
      INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) RecordDeclassifiedLoad,
			       IARG_MEMORYOP_EA, memOp,
			       IARG_UINT32, eff_size, IARG_END);
    } else if (INS_MemoryOperandIsWritten(ins, memOp)) {
      if (declassified) {
	INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) RecordDeclassifiedStore,
				IARG_MEMORYOP_EA, memOp,
				 IARG_UINT32, eff_size, IARG_END);
      } else {
	INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) RecordClassifiedStore,
				 IARG_INST_PTR, IARG_MEMORYOP_EA, memOp,
				 IARG_UINT32, eff_size, IARG_END);
      }
    }
  }
}

static void Fini(int32_t code, void *v) {
  fprintf(stderr, "%lu distinct accesses\n", staticAccessCount);
  fprintf(stderr, "hits %lu\n", decltab_hits);
  fprintf(stderr, "misses-cold %lu\n", decltab_misses_cold);
  unsigned long decltab_misses_classified = 0;
  shadow_store_inst_classify_miss.for_each([&decltab_misses_classified] (ADDRINT, unsigned long count) {
    decltab_misses_classified += count;
  });
  fprintf(stderr, "misses-classify %lu\n", decltab_misses_classified);
  fprintf(stderr, "hits-page %lu\n", decltab_hits_page);

  // Print 10 top store instructions
  std::vector<std::pair<unsigned long, ADDRINT>> classified_stores;
  shadow_store_inst_classify_miss.for_each([&classified_stores] (ADDRINT store_inst, unsigned long count) {
    classified_stores.emplace_back(count, store_inst);
  });
  std::sort(classified_stores.begin(), classified_stores.end());
  auto classified_store_it = classified_stores.rbegin();
  for (unsigned i = 0; i < 10 && classified_store_it != classified_stores.rend(); ++i, ++classified_store_it) {
    fprintf(stderr, "store-classify-miss-hist-%u %" PRIx64 " %lu\n", i, classified_store_it->second, classified_store_it->first);
  }

  shadow_declassified_pages.for_each([] (ADDRINT, int count) {
    if (count < 0 || static_cast<unsigned>(count) > shadow_declassified_pages.ValueSize) {
      PIN_ERROR("bad declassified byte count in page");
    }
  });
}

static int32_t usage() {
  PIN_ERROR("LLSCT declassification profiler\n");
  std::cerr << KNOB_BASE::StringKnobSummary() << "\n";
  return -1;
}

static char buffer[1024 * 1024 * 1024];

int main(int argc, char *argv[]) {
  if (PIN_Init(argc, argv))
    return usage();
  string filename = OutputFile.Value();
  if (filename.empty())
    return usage();
  trace = fopen(filename.c_str(), "w");
  errno = 0;
  if (setvbuf(trace, buffer, _IOFBF, sizeof buffer)) {
    if (errno)
      perror("setvbuf");
    else
      fprintf(stderr, "setvbuf failed\n");
    return 1;
  }
  INS_AddInstrumentFunction(Instruction, 0);
  PIN_AddFiniFunction(Fini, 0);
  PIN_StartProgram();
}
