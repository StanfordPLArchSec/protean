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
#include "Declassify.h"
using std::cerr;
using std::endl;
using std::string;

KNOB<string> OutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "specify file name for output");
FILE *trace;

static void RecordAccess(void *inst, void *addr, uint32_t mode) {
  Record record;
  record.inst = inst;
  record.addr = addr;
  record.mode = static_cast<uint8_t>(mode);
  fwrite(&record, sizeof record, 1, trace);
}

static void MemoryAccess(INS ins, uint32_t memOp, uint8_t access) {
  if (INS_HasScatteredMemoryAccess(ins))
    return;
  
  uint8_t taint = TT_CLASSIFY;

  char buf[16];
  PIN_SafeCopy(buf, (void *) INS_Address(ins), INS_Size(ins));
  // Custom prefix scanner
  // FIXME: This is too hacky
  if (buf[0] == 0x36 /*SS*/ || (buf[0] == 0x66 && buf[1] == 0x36)) {
    taint = TT_DECLASSIFY;
    // fprintf(stderr, "declassify: %s\n", INS_Disassemble(ins).c_str());
  }
  uint8_t mode = access | taint | (INS_MemoryOperandSize(ins, memOp) << 2);
  INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordAccess,
			   IARG_INST_PTR, IARG_MEMORYOP_EA, memOp,
			   IARG_UINT32, mode, IARG_END);
}


static void Instruction(INS ins, void *v) {
  uint32_t memOperands = INS_MemoryOperandCount(ins);

  for (uint32_t memOp = 0; memOp < memOperands; ++memOp) {
    if (INS_MemoryOperandIsRead(ins, memOp))
      MemoryAccess(ins, memOp, AC_READ);
    if (INS_MemoryOperandIsWritten(ins, memOp))
      MemoryAccess(ins, memOp, AC_WRITE);
  }
}

static int32_t usage() {
  PIN_ERROR("LLSCT declassification profiler\n");
  std::cerr << KNOB_BASE::StringKnobSummary() << "\n";
  return -1;
}


int main(int argc, char *argv[]) {
  if (PIN_Init(argc, argv))
    return usage();
  string filename = OutputFile.Value();
  if (filename.empty())
    return usage();
  trace = fopen(filename.c_str(), "w");
  INS_AddInstrumentFunction(Instruction, 0);
  PIN_StartProgram();
}
