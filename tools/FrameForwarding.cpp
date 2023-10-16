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
using std::cerr;
using std::endl;
using std::string;

/* ================================================================== */
// Global variables
/* ================================================================== */

UINT64 insCount    = 0; //number of dynamically executed instructions
UINT64 bblCount    = 0; //number of dynamically executed basic blocks
UINT64 threadCount = 0; //total number of threads, including main thread

std::ostream* out = &cerr;

/* ===================================================================== */
// Command line switches
/* ===================================================================== */
KNOB< string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "specify file name for MyPinTool output");
KNOB< string > KnobAppend(KNOB_MODE_WRITEONCE, "pintool", "a", "", "append to output");

/* ===================================================================== */
// Utilities
/* ===================================================================== */

/*!
 *  Print out help message.
 */
INT32 Usage()
{
    cerr << "This tool prints out the number of dynamically executed " << endl
         << "instructions, basic blocks and threads in the application." << endl
         << endl;

    cerr << KNOB_BASE::StringKnobSummary() << endl;

    return -1;
}

static std::map<std::string, size_t> counts;

static void Record(const char *key) {
  counts[key]++;
}

static void Instruction(INS ins, void *null) {
  const auto insert_record = [&] (const char *key) {
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) Record, IARG_PTR, key, IARG_END);
  };
  
  // Check for implicit stack loads.
  switch (INS_Category(ins)) {
  case XED_CATEGORY_POP:
    insert_record("pop");
    return; 
  case XED_CATEGORY_RET:
    insert_record("ret");
    return;
  }

  // Check for explicit loads.
  uint32_t memOperands = INS_MemoryOperandCount(ins);
  for (uint32_t i = 0; i < memOperands; ++i) {
    if (INS_IsMemoryRead(ins)) {
      if (INS_MemoryBaseReg(ins) == REG_RSP && INS_MemoryIndexReg(ins) == REG_INVALID()) {
	insert_record("frame");
      } else {
	insert_record("nca");
      }
    }
  }
}

/*!
 * Print out analysis results.
 * This function is called when the application exits.
 * @param[in]   code            exit code of the application
 * @param[in]   v               value specified by the tool in the 
 *                              PIN_AddFiniFunction function call
 */
VOID Fini(INT32 code, VOID* v)
{
  for (const auto& [key, value] : counts) {
    *out << key << " " << value << "\n";
  }
  out->flush();
}

/*!
 * The main procedure of the tool.
 * This function is called when the application image is loaded but not yet started.
 * @param[in]   argc            total number of elements in the argv array
 * @param[in]   argv            array of command line arguments, 
 *                              including pin -t <toolname> -- ...
 */
int main(int argc, char* argv[])
{
  PIN_InitSymbols();
  if (PIN_Init(argc, argv))
    return Usage();

  string fileName = KnobOutputFile.Value();
  
  if (!fileName.empty()) {
    std::ios::openmode mode = std::ios::out;
    if (!KnobAppend.Value().empty())
      mode = std::ios::app;
    out = new std::ofstream(fileName.c_str(), mode);
  }

  INS_AddInstrumentFunction(&Instruction, 0);
  PIN_AddFiniFunction(&Fini, 0);

  PIN_StartProgram();

  return 0;
}
