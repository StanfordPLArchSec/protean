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
static constexpr size_t kAlignment = 16;

struct Allocation {
  
  ADDRINT base;
  ADDRINT size;

  Allocation(ADDRINT base, ADDRINT size): base(base), size(size) {}
};
using AllocationList = std::list<Allocation>;
using AllocationIt = AllocationList::iterator;

static std::list<Allocation> allocations;
static ShadowMemory<AllocationIt, 4, kAlignment> shadow(allocations.end());

static void handle_alloc(ADDRINT base, ADDRINT size) {
  allocations.push_front(Allocation(base, size));
  auto it = allocations.begin();
  assert(base % kAlignment == 0); // malloc should return 16-byte-aligned allocations.
  for (ADDRINT ptr = base; ptr < base + it->size; ptr += kAlignment) {
    assert(shadow[ptr] == allocations.end());
    shadow[ptr] = it;
  }
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

static void Handle_realloc(void *ptr, ADDRINT size) {
  log() << "realloc(" << ptr << ", " << size << ")\n";
}

static void Handle_reallocarray(void *ptr, ADDRINT nmemb, ADDRINT size) {
  log() << "reallocarray(" << ptr << ", " << nmemb << ", " << size << ")\n";
}

static void Handle_free(ADDRINT base) {
  log() << "free(" << (void *) base << ")\n";
  auto it = shadow[base];
  assert(it != allocations.end());
  assert(it->base == base);
  for (ADDRINT ptr = base; ptr < base + it->size; ptr += kAlignment) {
    assert(shadow[ptr] == it);
    shadow[ptr] = allocations.end();
  }
  allocations.erase(it);
}

static void PrintRoutineName(const char *s) {
  log() << s << "\n";
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
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR) Handle_realloc,
		   IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
		   IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
		   IARG_END);
    RTN_Close(rtn);
  } else if (name == "__wrap_reallocarray") {
    RTN_Open(rtn);
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR) Handle_reallocarray,
		   IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
		   IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
		   IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
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

  log_.open(LogFile.Value());

  PIN_AddFiniFunction(Fini, nullptr);
  PIN_StartProgram();
}
