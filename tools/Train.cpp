#include <cstdlib>
#include <iostream>
#include <pin.H>
#include <optional>
#include <cassert>
#include <list>
#include <fstream>

#include "ShadowMemory.h"

static KNOB<std::string> LogFile(KNOB_MODE_WRITEONCE, "pintool", "l", "", "log file");

static std::ofstream log_;
static std::ostream &log() {
  if (LogFile.Value().empty())
    return std::cerr;
  else
    return log_;
}

// TODO: Support multi-threaded programs.
static constexpr size_t kAlignmentBits = 4;
static constexpr size_t kAlignment = 1 << kAlignmentBits;



class Allocation {
public:
  Allocation(ADDRINT base, ADDRINT size): base_(base), size_(size), taint(size, true) {}

  ADDRINT base() const { return base_; }
  ADDRINT size() const { return size_; }
  ADDRINT end() const { return base() + size(); }
  void realloc(size_t newbase, size_t newsize) {
    taint.resize(newsize, true);
    size_ = newsize;
    base_ = newbase;
  }
  void markPrivate(size_t effbase, size_t effsize) {
    const auto effend = effbase + effsize;
    assert(effbase >= base() && effbase + effsize <= end());
    for (size_t effaddr = effbase; effaddr < effend; ++effaddr) {
      taint[effaddr - base()] = false;
    }
  }

  void print(std::ostream &os) const {
    os << "base=" << (void *) base() << ", size=" << size() << ", taint=";
    for (bool t : taint)
      os << t;
  }

private:
  ADDRINT base_;
  ADDRINT size_;
  std::vector<bool> taint; // true = public, false = private.

};
using AllocationList = std::list<Allocation>;
using AllocationIt = AllocationList::iterator;

static std::list<Allocation> allocations;
static ShadowMemory<AllocationIt, kAlignmentBits, 16> shadow(allocations.end());

static void handle_alloc(ADDRINT base, ADDRINT size) {
  allocations.push_front(Allocation(base, size));
  auto it = allocations.begin();
  assert(base % kAlignment == 0); // malloc should return 16-byte-aligned allocations.
  for (ADDRINT ptr = it->base(); ptr < it->end(); ptr += kAlignment) {
    assert(shadow[ptr] == allocations.end());
    shadow[ptr] = it;
  }
}

static void handle_realloc(ADDRINT oldbase, ADDRINT newbase, ADDRINT newsize) {
  assert(oldbase);
  assert(newbase);

  auto it = shadow[oldbase];
  assert(it != allocations.end());
  assert(it->base() == oldbase);
  const ADDRINT oldsize = it->size();

  // Reset old allocation region.
  for (ADDRINT oldptr = oldbase; oldptr < oldbase + oldsize; oldptr += kAlignment) {
    auto &itref = shadow[oldptr];
    assert(itref == it);
    itref = allocations.end();
  }

  // Fill newly allocated region with same old iterators.
  for (ADDRINT newptr = newbase; newptr < newbase + newsize; newptr += kAlignment) {
    auto &itref = shadow[newptr];
    assert(itref == allocations.end());
    itref = it;
  }

  // Update allocation object.
  it->realloc(newbase, newsize);
}

static size_t arg_malloc_size;
static void Handle_malloc_before(ADDRINT size) {
  arg_malloc_size = size;
  log() << "malloc(" << size << ") = ";
}
static void Handle_malloc_after(ADDRINT base) {
  log() << (void *) base << "\n";
  if (base) 
    handle_alloc(base, arg_malloc_size);
}

static size_t arg_calloc_nmemb, arg_calloc_size;
static void Handle_calloc_before(ADDRINT nmemb, ADDRINT size) {
  arg_calloc_nmemb = nmemb;
  arg_calloc_size = size;
  log() << "calloc(" << nmemb << ", " << size << ") = ";
}

static void Handle_calloc_after(ADDRINT base) {
  log() << (void *) base << "\n";
  if (base)
    handle_alloc(base, arg_calloc_nmemb * arg_calloc_size);
}

static ADDRINT arg_realloc_oldbase;
static size_t arg_realloc_newsize;
static void Handle_realloc_before(ADDRINT ptr, ADDRINT size) {
  arg_realloc_oldbase = ptr;
  arg_realloc_newsize = size;
  log() << "realloc(" << (void *) ptr << ", " << size << ") = ";
}

static void Handle_realloc_after(ADDRINT newbase) {
  log() << (void *) newbase << "\n";
  if (!newbase)
    return;
  if (arg_realloc_oldbase)
    handle_realloc(arg_realloc_oldbase, newbase, arg_realloc_newsize);
  else
    handle_alloc(newbase, arg_realloc_newsize);
}


static ADDRINT arg_reallocarray_ptr;
static ADDRINT arg_reallocarray_nmemb;
static ADDRINT arg_reallocarray_size;
static void Handle_reallocarray_before(ADDRINT ptr, ADDRINT nmemb, ADDRINT size) {
  arg_reallocarray_ptr = ptr;
  arg_reallocarray_nmemb = nmemb;
  arg_reallocarray_size = size;
  log() << "reallocarray(" << (void *) ptr << ", " << nmemb << ", " << size << ") = ";
}

static void Handle_reallocarray_after(ADDRINT newbase) {
  log() << (void *) newbase << "\n";
  if (!newbase)
    return;
  const size_t newsize = arg_reallocarray_nmemb * arg_reallocarray_size;
  if (arg_realloc_oldbase)
    handle_realloc(arg_reallocarray_ptr, newbase, newsize);
  else
    handle_alloc(newbase, newsize);
}

static void Handle_free(ADDRINT base) {
  log() << "free(" << (void *) base << ") = ";
  if (!base)
    return;
  auto it = shadow[base];
  assert(it != allocations.end());
  assert(it->base() == base);
  for (ADDRINT ptr = base; ptr < it->end(); ptr += kAlignment) {
    assert(shadow[ptr] == it);
    shadow[ptr] = allocations.end();
  }

  // Print allocation info.
  it->print(log());
  log() << "\n";
  
  allocations.erase(it);
}


static void RecordPrivateStore(ADDRINT effaddr, UINT32 effsize) {
  assert(effsize > 0);
  assert(effsize <= kAlignment);

  // If spans 16-byte boundary, split into two accesses.
  if (effaddr / kAlignment != (effaddr + effsize - 1) / kAlignment) {
    const ADDRINT lo_begin = effaddr;
    const ADDRINT lo_end = (effaddr / kAlignment + 1) * kAlignment;
    const ADDRINT hi_begin = lo_end;
    const ADDRINT hi_end = effaddr + effsize;
    RecordPrivateStore(lo_begin, lo_end - lo_begin);
    RecordPrivateStore(hi_begin, hi_end - hi_begin);
    return;
  }

  // Is it in allocated memory?
  const AllocationIt it = shadow[effaddr];
  if (it == allocations.end()) {
    // std::cerr << "not in heap: " << (void *) effaddr << "\n";
    return;
  }
  
  it->markPrivate(effaddr, effsize);
}


static void PrintRoutineName(const char *s) {
  log() << s << "\n";
}

static void HandleInstruction(INS ins, void *) {
  const auto num_memops = INS_MemoryOperandCount(ins);
  if (num_memops == 0)
    return;

  if (INS_HasScatteredMemoryAccess(ins)) {
    std::cerr << "don't handle scattered memory accesses yet!\n";
    abort();
  }

  bool is_public = true;
  {
    char buf[16];
    PIN_SafeCopy(buf, (void *) INS_Address(ins), INS_Size(ins));
    if (buf[0] == 0x36 /*SS*/ || (buf[0] == 0x66 && buf[1] == 0x36)) {
      is_public = false;
    }
  }
  if (is_public)
    return;

  for (auto i = 0; i < num_memops; ++i) {
    const auto eff_size = INS_MemoryOperandSize(ins, i);
    if (!INS_MemoryOperandIsWritten(ins, i))
      continue;
    INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) RecordPrivateStore,
			     IARG_MEMORYOP_EA, i,
			     IARG_UINT32, eff_size,
			     IARG_END);
  }
}

static void HandleRoutine(RTN rtn, void *) {
  const std::string &name = RTN_Name(rtn);
  if (name == "__wrap_malloc") {
    RTN_Open(rtn);
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR) Handle_malloc_before,
		   IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);
    RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR) Handle_malloc_after,
		   IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);
    RTN_Close(rtn);
  } else if (name == "__wrap_calloc") {
    RTN_Open(rtn);
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR) Handle_calloc_before,
		   IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
		   IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
		   IARG_END);
    RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR) Handle_calloc_after,
		   IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);
    RTN_Close(rtn);
  } else if (name == "__wrap_realloc") {
    RTN_Open(rtn);
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR) Handle_realloc_before,
		   IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
		   IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
		   IARG_END);
    RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR) Handle_realloc_after,
		   IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);
    RTN_Close(rtn);
  } else if (name == "__wrap_reallocarray") {
    RTN_Open(rtn);
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR) Handle_reallocarray_before,
		   IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
		   IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
		   IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
		   IARG_END);
    RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR) Handle_reallocarray_after,
		   IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);
    RTN_Close(rtn);
  } else if (name == "__wrap_free") {
    RTN_Open(rtn);
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR) Handle_free,
		   IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
		   IARG_END);
    RTN_Close(rtn);
  }


#if 0
  // DEBUG ONLY
  RTN_Open(rtn);
  const char *s = strdup(name.c_str());
  RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR) PrintRoutineName, IARG_PTR, s, IARG_END);
  RTN_Close(rtn);
#endif
}

static void usage(std::ostream &os) {
  os << KNOB_BASE::StringKnobSummary() << "\n";
}

static void Fini(int32_t code, void *) {
  log_.close();
}

int main(int argc, char *argv[]) {
  if (PIN_Init(argc, argv)) {
    usage(std::cerr);
    return EXIT_FAILURE;
  }
#if 1
  PIN_InitSymbols();
#else
  PIN_InitSymbolsAlt(SYMBOL_INFO_MODE(UINT32(IFUNC_SYMBOLS) | UINT32(DEBUG_OR_EXPORT_SYMBOLS)));
#endif

  RTN_AddInstrumentFunction(HandleRoutine, nullptr);
  INS_AddInstrumentFunction(HandleInstruction, nullptr);

  log_.open(LogFile.Value());

  PIN_AddFiniFunction(Fini, nullptr);
  PIN_StartProgram();
}
