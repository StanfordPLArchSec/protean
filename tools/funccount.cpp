#include <string>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include "pin.H"

static KNOB<std::string> OutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "specify output file");
static std::ofstream out;

static std::unordered_map<std::string, unsigned long> FuncCounts;

static void DynamicRoutine(unsigned long &count) {
  ++count;
}

static void StaticRoutine(RTN rtn, void *) {
  RTN_Open(rtn);
  unsigned long &count = FuncCounts[RTN_Name(rtn)];
  RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR) DynamicRoutine, IARG_PTR, &count, IARG_END);
  RTN_Close(rtn);
}

static void Finish(int32_t code, void *) {
  for (const auto &[name, count] : FuncCounts)
    if (count)
      out << name << " " << count << "\n";
  out.close();
}

int main(int argc, char *argv[]) {
  if (PIN_Init(argc, argv)) {
    std::cerr << KNOB_BASE::StringKnobSummary() << "\n";
    return EXIT_FAILURE;
  }

  PIN_InitSymbols();

  if (OutputFile.Value().empty()) {
    std::cerr << "funccount: -o: required\n";
    return EXIT_FAILURE;
  }
  out.open(OutputFile.Value());

  RTN_AddInstrumentFunction(StaticRoutine, nullptr);
  PIN_AddFiniFunction(Finish, nullptr);
  PIN_StartProgram();
}
