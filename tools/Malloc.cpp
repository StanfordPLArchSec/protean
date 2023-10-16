#include "pin.H"
#include <set>
#include <string>
#include <map>
#include <cstdio>

static FILE *f;
typedef int handle_t;
static std::map<void *, handle_t> allocs; // live allocations

void *malloc_func(size_t size) {
  static handle_t next_handle = 0;
  const handle_t handle = next_handle++;
  fprintf(f, "void *p%d = malloc(%zu);\n",
	  handle, size);
}

void HandleMalloc(ADDRINT rdi_size) {

}

void InstrumentRoutine(RTN rtn, void *) {
  if (RTN_Name(rtn) == "malloc") {
    
  }
}

int main(int argc, char *argv[]) {
  PIN_InitSymbols();
  if (PIN_Init(argc, argv))
    return 1;

  RTN_AddInstrumentFunction(InstrumentRoutine, nullptr);
}
