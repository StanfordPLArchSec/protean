#include "pin.H"
#include <fstream>
#include <string>
#include <vector>
#include <iostream>
#include <set>

static const char *prog = "functoinst";

static KNOB<std::string> InFile(KNOB_MODE_WRITEONCE, "pintool", "i", "", "input file containing (func count begin, func count end) pairs");
static KNOB<std::string> OutFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "output file containing corresponding (inst count begin, inst count end) pairs");
static KNOB<bool> EarlyExit(KNOB_MODE_WRITEONCE, "pintool", "allow-early-exit", "0", "allow early exit, once all functions have been mapped to instructions");

static std::vector<uint64_t> func_counts;
static std::vector<uint64_t>::iterator func_counts_it;
static uint64_t func_count_tgt; // Current function count target.
static uint64_t func_count_cur = 0; // Actual current function count.
static unsigned long inst_count = 0;
static std::ofstream out;

static void emit_func_to_inst(ADDRINT func, ADDRINT inst) {
  out << func << " " << inst << std::endl;
}

static void advance_func_count() {
  if (func_counts_it == func_counts.end()) {
    // Mapped all requested func->inst, so exit.
    func_count_tgt = 0;
    if (EarlyExit.Value())
      PIN_ExitApplication(0);
    return;
  }
  func_count_tgt = *func_counts_it++;
  assert(func_count_cur < func_count_tgt);
}

static void DynamicRoutine() {
  ++func_count_cur;
  if (func_count_cur == func_count_tgt) {
    emit_func_to_inst(func_count_cur, inst_count);
    advance_func_count();
  }
}

static void StaticRoutine(RTN rtn, void *) {
  RTN_Open(rtn);
  RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR) DynamicRoutine, IARG_END);
  RTN_Close(rtn);
}

static void DynamicBlock(ADDRINT num_insts) {
  inst_count += num_insts;
}

static void StaticTrace(TRACE trace, void *) {
  for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR) DynamicBlock, IARG_ADDRINT, BBL_NumIns(bbl), IARG_END);
}

static void Fini(int32_t exit_code, void *) {
  out << "# function-count-at-end " << func_count_cur << std::endl;
  if (func_count_tgt) {
    out << "FAILED: didn't hit all function calls" << std::endl;
    std::exit(EXIT_FAILURE);
  }
  out.close();
}

int main(int argc, char *argv[]) {
  if (PIN_Init(argc, argv)) {
    std::cerr << KNOB_BASE::StringKnobSummary() << "\n";
    return EXIT_FAILURE;
  }
  PIN_InitSymbols();

  if (OutFile.Value().empty()) {
    std::cerr << prog << ": -o: required\n";
    return EXIT_FAILURE;
  }
  out.open(OutFile.Value());

  if (InFile.Value().empty()) {
    std::cerr << prog << ": -o: required\n";
    return EXIT_FAILURE;
  }
  std::ifstream in(InFile.Value());
  std::string line;
  while (std::getline(in, line))
    func_counts.push_back(std::stoull(line));
  if (func_counts.empty()) {
    std::cerr << prog <<": -i: empty file!\n";
    return EXIT_FAILURE;
  }
  for (uint64_t &func_count : func_counts)
    assert(func_count);
  func_counts_it = func_counts.begin();
  advance_func_count();
  if (func_count_tgt == 0) {
    emit_func_to_inst(0, 0);
    advance_func_count();
  }
  assert(func_count_tgt);

  RTN_AddInstrumentFunction(StaticRoutine, nullptr);
  TRACE_AddInstrumentFunction(StaticTrace, nullptr);
  PIN_AddFiniFunction(Fini, nullptr);
  PIN_StartProgram();
}
