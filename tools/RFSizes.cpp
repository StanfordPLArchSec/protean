#include "pin.H"
#include "ShadowMemory.h"
#include <string>
#include <fstream>
#include <iostream>

static KNOB<std::string> OutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "specify file name for output");

static ShadowMemory<uint8_t, 0, 12> shadow(0);
static unsigned long stat_matches = 0;
static unsigned long stat_mismatches = 0;

static void RecordWrite(ADDRINT addr, uint32_t size) {
  // We'll just align the pointers. This will reduce accuracy a bit,
  // but probably not by much. There shouldn't be many misaligned pointers
  // to begin with.
  assert(size < 256);
  addr &= ~static_cast<ADDRINT>(size - 1);
  uint8_t *ptr = &shadow[addr];
  std::fill_n(ptr, size, static_cast<uint8_t>(size));
}

static void RecordRead(ADDRINT addr, uint32_t size) {
  assert(size < 256);
  addr &= ~static_cast<ADDRINT>(size - 1);
  const uint8_t *ptr = &shadow[addr];
  const bool match = std::all_of(ptr, ptr + size, [size] (uint8_t st_size) {
    return st_size == size;
  });
  if (match)
    ++stat_matches;
  else
    ++stat_mismatches;
}

static void Instruction(INS ins, void *) {
  uint32_t memops = INS_MemoryOperandCount(ins);
  if (INS_HasScatteredMemoryAccess(ins))
    return;

  for (uint32_t memop = 0; memop < memops; ++memop) {
    const uint32_t size = INS_MemoryOperandSize(ins, memop);
    if (INS_MemoryOperandIsRead(ins, memop))
      INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) RecordRead, IARG_MEMORYOP_EA, memop, IARG_UINT32, size, IARG_END);
    if (INS_MemoryOperandIsWritten(ins, memop))
      INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) RecordWrite, IARG_MEMORYOP_EA, memop, IARG_UINT32, size, IARG_END);
  }
}


static int usage() {
  std::cerr << KNOB_BASE::StringKnobSummary << "\n";
  return 1;
}

static void Fini(int32_t code, void *) {
  std::ofstream os(OutputFile.Value());
  os << "matches " << stat_matches << "\n";
  os << "mismatches " << stat_mismatches << "\n";
  os << "mismatch-rate " << (static_cast<float>(stat_mismatches) / (stat_matches + stat_mismatches)) << "\n";
}

int main(int argc, char *argv[]) {
  if (PIN_Init(argc, argv))
    return usage();

  if (OutputFile.Value().empty())
    return usage();

  INS_AddInstrumentFunction(Instruction, nullptr);
  PIN_AddFiniFunction(Fini, nullptr);
  PIN_StartProgram();
}
