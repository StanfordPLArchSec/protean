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
#include "ShadowDeclassificationTable.h"
#include "PatternDeclassificationTable.h"

static KNOB<std::string> OutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "specify file name for output");
static KNOB<unsigned long> Interval(KNOB_MODE_WRITEONCE, "pintool", "i", "0", "interval (default: 50M)");
static KNOB<long> MaxInst(KNOB_MODE_WRITEONCE, "pintool", "m", "-1", "max inst");
static KNOB<std::string> OutputDir(KNOB_MODE_WRITEONCE, "pintool", "d", "", "output directory");
static KNOB<unsigned> Coarsen(KNOB_MODE_WRITEONCE, "pintool", "c", "0", "coarsen all accesses to this granuarity");
static std::ofstream out;

unsigned long hits = 0;
unsigned long misses = 0;

unsigned long interval;
long max_inst;
unsigned coarsen;

#if 0
static ShadowDeclassificationTable decltab;
#elif 0
static ParallelDeclassificationTable</*LineSize*/8, /*TableSize*/256*256, /*NumTables*/3, /*Associativity*/2> decltab;
#elif 0
static ParallelDeclassificationTable
<
  /*NumTables*/3,
  /*LineSize*/{8, 128, 64},
  /*TableSize*/{512, 1536, 256},
  /*Associativity*/{4, 6, 4}
>
decltab;
#else
static DeclassificationTable<64, 4, 1024, 32, true> decltab("eviction.log");
#endif

static void RecordDeclassifiedLoad(ADDRINT eff_addr, UINT32 eff_size) {
  // Check if aligned
  if ((eff_addr & (eff_size - 1)) != 0) {
    // Unaligned: split in two
    RecordDeclassifiedLoad(eff_addr, eff_size / 2);
    RecordDeclassifiedLoad(eff_addr + eff_size / 2, eff_size / 2);
    return;
  }

  // Check if coarsen.
  UINT32 old_eff_size = eff_size;
  if (eff_size < coarsen) {
    eff_addr &= ~(coarsen - 1);
    eff_size = coarsen;
  }
  
  if (decltab.checkDeclassified(eff_addr, eff_size)) {
    ++hits;
  } else {
    ++misses;
  }

  if (old_eff_size < coarsen)
    return;

  decltab.setDeclassified(eff_addr, eff_size);
}

static void RecordClassifiedStore(ADDRINT st_inst, ADDRINT eff_addr, ADDRINT eff_size) {
  // Check if aligned
  if ((eff_addr & (eff_size - 1)) != 0) {
    RecordClassifiedStore(st_inst, eff_addr, eff_size / 2);
    RecordClassifiedStore(st_inst, eff_addr + eff_size / 2, eff_size / 2);
    return;
  }

  // Check if we should coarsen
  if (eff_size < coarsen) {
    eff_addr &= ~(coarsen - 1);
    eff_size = coarsen;
  }
  
  decltab.setClassified(eff_addr, eff_size, st_inst);
}

static void RecordDeclassifiedStore(ADDRINT eff_addr, ADDRINT eff_size) {
  // Check if aligned
  if ((eff_addr & (eff_size - 1)) != 0) {
    RecordDeclassifiedStore(eff_addr, eff_size / 2);
    RecordDeclassifiedStore(eff_addr + eff_size / 2, eff_size / 2);
    return;
  }

  // Check if coarsen
  if (eff_size < coarsen) {
    return;
  }
  
  decltab.setDeclassified(eff_addr, eff_size);
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

#if 0
static void Handle_Interval() {
  static unsigned long counter = 0;
  if (counter % interval == 0) {
    decltab.clear();
  }
  ++counter;
}

static void Instruction_Stats(INS ins, VOID *) {
  INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) Handle_Interval, IARG_END);
}
#endif

static void Fini(int32_t, VOID *);
static void Handle_MaxInst() {
  static long counter = 0;
  if (counter == max_inst) {
    Fini(0, 0);
    PIN_ExitProcess(0);
  }
  ++counter;
}

static void Instruction_MaxInst(INS ins, VOID *) {
  INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) Handle_MaxInst, IARG_END);
}

static void Handle_Interval(UINT32 num_insts) {
  static unsigned long counter = 0;
  static unsigned long next = interval;
  if (counter >= next) {
    next += interval;

#if 0
    // Dump shadow memory
    char taint_path[256];
    sprintf(taint_path, "%s/%lu.taint", OutputDir.Value().c_str(), counter / interval);
    FILE *f_taint = fopen(taint_path, "w");
    if (f_taint == NULL) {
      perror("fopen");
      PIN_ExitProcess(1);
    }
    fprintf(stderr, "writing mem...\n");
    std::vector<uint8_t> taint;
    decltab.dumpTaint(taint);
    fwrite(taint.data(), 1, taint.size(), f_taint);
    fprintf(stderr, "done\n");
    fclose(f_taint);
#endif
  }
  counter += num_insts;
}

static void Trace_Interval(TRACE trace, VOID *) {
  for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
    BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR) Handle_Interval, IARG_UINT32, BBL_NumIns(bbl), IARG_END);
  }
}

static void Fini(int32_t code, void *v) {
  decltab.printDesc(out);
  out << "hits " << hits << "\n"
      << "misses " << misses << "\n"
      << "miss-rate " << (misses / static_cast<float>(hits + misses) * 100) << "\n";
  decltab.printStats(out);
  out.close();
}

static int32_t usage() {
  PIN_ERROR("LLSCT declassification profiler\n");
  std::cerr << KNOB_BASE::StringKnobSummary() << "\n";
  return -1;
}

int main(int argc, char *argv[]) {
  if (PIN_Init(argc, argv))
    return usage();
  std::string filename = OutputFile.Value();
  if (filename.empty())
    return usage();
  out.open(filename);
  max_inst = MaxInst.Value();
  if (max_inst >= 0)
    INS_AddInstrumentFunction(Instruction_MaxInst, 0);
  interval = Interval.Value();
  coarsen = Coarsen.Value();
  if (interval > 0)
    TRACE_AddInstrumentFunction(Trace_Interval, 0);
  // INS_AddInstrumentFunction(Instruction_ResetStats, 0);
  // INS_AddInstrumentFunction(Instruction_ClearTable, 0);
  INS_AddInstrumentFunction(Instruction, 0);
  PIN_AddFiniFunction(Fini, 0);
  PIN_StartProgram();
}
