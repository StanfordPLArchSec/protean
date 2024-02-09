#include <cstdlib>
#include <iostream>
#include <pin.H>
#include <optional>
#include <cassert>
#include <list>
#include <fstream>
#include <numeric>
#include <unordered_map>

#include "ShadowMemory.h"

static KNOB<std::string> LogFile(KNOB_MODE_WRITEONCE, "pintool", "l", "", "log file");
static KNOB<unsigned> Verbose(KNOB_MODE_WRITEONCE, "pintool", "v", "0", "verbose");

static unsigned verbose() {
  return Verbose.Value();
}

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

static void get_backtrace(const CONTEXT *ctx, std::vector<void *> &bt) {
  PIN_LockClient();
  bt.resize(16);
  while (PIN_Backtrace(ctx, bt.data(), bt.size()) == bt.size())
    bt.resize(bt.size() * 2);
  PIN_UnlockClient();
}

static void dump_backtrace(std::ostream &os, const CONTEXT *ctx) {
  std::vector<void *> bt;
  get_backtrace(ctx, bt);
  for (void *p : bt)
    os << "\t" << p << "\n";
}

class CallSite {
public:
  CallSite(size_t alloc_size): size_gcd(alloc_size) { ++num_allocs; }

  void observeAlloc(size_t size) {
    ++num_allocs;
    size_gcd = std::gcd(size_gcd, size);
  }
  void observeRealloc(size_t size) {
    observeAlloc(size);
  }

  size_t getSize() const { return size_gcd; }
  size_t getNumAllocs() const { return num_allocs; }

private:
  size_t size_gcd;
  size_t num_allocs = 0;
};

static std::vector<void *> backtrace;
static std::unordered_map<ADDRINT, CallSite> callsites;

class Allocation {
public:
  Allocation(ADDRINT base, ADDRINT size, std::vector<void *> &&backtrace): base_(base), size_(size), taint(size, true), callstack(std::move(backtrace)) {}

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

  const std::vector<void *> &getCallStack() const { return callstack; }
  
private:
  ADDRINT base_;
  ADDRINT size_;
  std::vector<bool> taint; // true = public, false = private.
  std::vector<void *> callstack;

};
using AllocationList = std::list<Allocation>;
using AllocationIt = AllocationList::iterator;

static std::list<Allocation> allocations;
static ShadowMemory<AllocationIt, kAlignmentBits, 16> shadow(allocations.end());

static void handle_alloc(ADDRINT base, ADDRINT size) {
  allocations.push_front(Allocation(base, size, std::move(backtrace)));
  auto it = allocations.begin();
  assert(base % kAlignment == 0); // malloc should return 16-byte-aligned allocations.
  for (ADDRINT ptr = it->base(); ptr < it->end(); ptr += kAlignment) {
    assert(shadow[ptr] == allocations.end());
    shadow[ptr] = it;
  }

  // Update callsites as necessary.
  for (void *callsite_ : it->getCallStack()) {
    const ADDRINT callsite = reinterpret_cast<ADDRINT>(callsite_);
    auto it = callsites.find(callsite);
    if (it == callsites.end())
      it = callsites.emplace(callsite, CallSite(size)).first;
    else
      it->second.observeAlloc(size);
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

  for (void *callsite_ : it->getCallStack()) {
    const ADDRINT callsite = reinterpret_cast<ADDRINT>(callsite_);
    auto it = callsites.find(callsite);
    if (it == callsites.end())
      it = callsites.emplace(callsite, CallSite(newsize)).first;
    else
      it->second.observeRealloc(newsize);
  }
}

// TODO: Assert no recursion!
static size_t arg_malloc_size;
static void Handle_malloc_before(ADDRINT size, const CONTEXT *ctx) {
  arg_malloc_size = size;
  if (verbose())
    log() << "malloc(" << size << ") = ";
  get_backtrace(ctx, backtrace);
}
static void Handle_malloc_after(ADDRINT base) {
  if (verbose())
    log() << (void *) base << "\n";
  if (base) 
    handle_alloc(base, arg_malloc_size);

}

static size_t arg_calloc_nmemb, arg_calloc_size;
static void Handle_calloc_before(ADDRINT nmemb, ADDRINT size, const CONTEXT *ctx) {
  arg_calloc_nmemb = nmemb;
  arg_calloc_size = size;
  if (verbose())
    log() << "calloc(" << nmemb << ", " << size << ") = ";
  get_backtrace(ctx, backtrace);
}

static void Handle_calloc_after(ADDRINT base) {
  if (verbose())
    log() << (void *) base << "\n";
  if (base)
    handle_alloc(base, arg_calloc_nmemb * arg_calloc_size);
}

static ADDRINT arg_realloc_oldbase;
static size_t arg_realloc_newsize;
static void Handle_realloc_before(ADDRINT ptr, ADDRINT size, const CONTEXT *ctx) {
  arg_realloc_oldbase = ptr;
  arg_realloc_newsize = size;
  if (verbose())
    log() << "realloc(" << (void *) ptr << ", " << size << ") = ";
  get_backtrace(ctx, backtrace);
}

static void Handle_realloc_after(ADDRINT newbase) {
  if (verbose())
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
static void Handle_reallocarray_before(ADDRINT ptr, ADDRINT nmemb, ADDRINT size, const CONTEXT *ctx) {
  arg_reallocarray_ptr = ptr;
  arg_reallocarray_nmemb = nmemb;
  arg_reallocarray_size = size;
  if (verbose())
    log() << "reallocarray(" << (void *) ptr << ", " << nmemb << ", " << size << ") = ";
  get_backtrace(ctx, backtrace);
}

static void Handle_reallocarray_after(ADDRINT newbase, const CONTEXT *ctx) {
  if (verbose())
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
  if (verbose())
    log() << "free(" << (void *) base << ") = ";
  if (!base) {
    if (verbose())
      log() << "\n";
    return;
  }
  auto it = shadow[base];
  assert(it != allocations.end());
  assert(it->base() == base);
  for (ADDRINT ptr = base; ptr < it->end(); ptr += kAlignment) {
    assert(shadow[ptr] == it);
    shadow[ptr] = allocations.end();
  }

  // Print allocation info.
  if (verbose()) {
    it->print(log());
    log() << "\n";
  }
  
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
		   IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
		   IARG_CONST_CONTEXT,
		   IARG_END);
    RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR) Handle_malloc_after,
		   IARG_FUNCRET_EXITPOINT_VALUE,
		   IARG_END);
    RTN_Close(rtn);
  } else if (name == "__wrap_calloc") {
    RTN_Open(rtn);
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR) Handle_calloc_before,
		   IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
		   IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
		   IARG_CONST_CONTEXT,
		   IARG_END);
    RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR) Handle_calloc_after,
		   IARG_FUNCRET_EXITPOINT_VALUE,
		   IARG_END);
    RTN_Close(rtn);
  } else if (name == "__wrap_realloc") {
    RTN_Open(rtn);
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR) Handle_realloc_before,
		   IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
		   IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
		   IARG_CONST_CONTEXT,
		   IARG_END);
    RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR) Handle_realloc_after,
		   IARG_FUNCRET_EXITPOINT_VALUE,
		   IARG_END);
    RTN_Close(rtn);
  } else if (name == "__wrap_reallocarray") {
    RTN_Open(rtn);
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR) Handle_reallocarray_before,
		   IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
		   IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
		   IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
		   IARG_CONST_CONTEXT,
		   IARG_END);
    RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR) Handle_reallocarray_after,
		   IARG_FUNCRET_EXITPOINT_VALUE,
		   IARG_END);
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
  // Classify callsites.
  for (const auto &[inst, callsite] : callsites) {
    if (callsite.getSize() == 1)
      continue;
    log() << (void *) inst << " " << callsite.getNumAllocs() << " " << callsite.getSize() << "\n";
  }
  
  
  log_.close();
}

int main(int argc, char *argv[]) {
  if (PIN_Init(argc, argv)) {
    usage(std::cerr);
    return EXIT_FAILURE;
  }
  PIN_InitSymbols();

  RTN_AddInstrumentFunction(HandleRoutine, nullptr);
  INS_AddInstrumentFunction(HandleInstruction, nullptr);

  log_.open(LogFile.Value());

  PIN_AddFiniFunction(Fini, nullptr);
  PIN_StartProgram();
}
