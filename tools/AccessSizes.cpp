#include "pin.H"

#include <string>
#include <iostream>
#include <fstream>
#include <array>

static KNOB<std::string> OutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "specify file name for output");

static std::ofstream out;

static std::array<std::array<std::array<unsigned long, 2>, 2>, 65> stats;

static void RecordStore(uint32_t declassified, uint32_t size) {
  stats[size][declassified][1]++;
}

static void RecordLoad(uint32_t declassified, uint32_t size) {
  stats[size][declassified][0]++;
}

static void Instruction(INS ins, VOID *) {
  UINT32 memops = INS_MemoryOperandCount(ins);
  if (INS_HasScatteredMemoryAccess(ins))
    return;

  char buf[2] = {0, 0};
  bool declassified = false;
  PIN_SafeCopy(buf, (void *) INS_Address(ins), std::min<unsigned>(INS_Size(ins), sizeof buf));
  if (buf[0] == 0x36 /*SS*/ || (buf[0] == 0x66 && buf[1] == 0x36))
    declassified = true;

  for (uint32_t memop = 0; memop < memops; ++memop) {
    const uint32_t size = INS_MemoryOperandSize(ins, memop);
    assert(1 <= size && size <= 64);
    if (INS_MemoryOperandIsRead(ins, memop))
      INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) RecordLoad, IARG_UINT32, static_cast<uint32_t>(declassified), IARG_UINT32, size, IARG_END);
    if (INS_MemoryOperandIsWritten(ins, memop))
      INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR) RecordStore, IARG_UINT32, static_cast<uint32_t>(declassified), IARG_UINT32, size, IARG_END);      
  }
}


static void printStats(std::ostream& os) {
  for (uint32_t size = 1; size < 65; ++size) {
    for (uint32_t declassified = 0; declassified < 2; ++declassified) {
      for (uint32_t write = 0; write < 2; ++write) {
	os << size << " " << (declassified ? "d" : "c") << " " << (write ? "w" : "r") << stats[size][declassified][write] << "\n";
      }
    }
  }
}


static void Fini(int32_t code, void *) {
  printStats(out);
}

static int usage() {
    std::cerr << KNOB_BASE::StringKnobSummary() << "\n";
    return 1;
}

int main(int argc, char *argv[]) {
  if (PIN_Init(argc, argv))
    return usage();

  if (OutputFile.Value().empty()) {
    std::cerr << "missing argument: -o\n";
    return usage();
  }
  out.open(OutputFile.Value());

  INS_AddInstrumentFunction(Instruction, 0);
  PIN_AddFiniFunction(Fini, 0);
  PIN_StartProgram();
}
