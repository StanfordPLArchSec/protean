#include <pin.H>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <cstdint>
#include <fstream>

static KNOB<std::string> OutFile(KNOB_MODE_WRITEONCE, "pintool", "out", "", "specify output path");
static KNOB<std::string> InFile(KNOB_MODE_WRITEONCE, "pintool", "in", "", "specify input path");
static std::unordered_map<ADDRINT, std::pair<std::string, uint64_t>> counts;
static std::ofstream out;
static ADDRINT img_base;

static void PIN_FAST_ANALYSIS_CALL AnalyzeINS(uint64_t &count) {
  ++count;
}

static void InstrumentINS(INS ins, void *) {
  ADDRINT addr = INS_Address(ins);
  IMG img = IMG_FindByAddress(addr);
  if (!IMG_Valid(img))
    return;
  addr -= IMG_LoadOffset(img);
  const auto it = counts.find(addr);
  if (it == counts.end())
    return;
  uint64_t *ptr = &it->second.second;
  INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) AnalyzeINS,
                 IARG_FAST_ANALYSIS_CALL,
                 IARG_PTR, ptr,
                 IARG_END);
}

static void Finish(int32_t code, void *) {
  for (const auto &[addr, p] : counts)
    if (p.second > 0)
      out << "0x" << std::hex << addr << " " << std::dec << p.second << " " << p.first << "\n";
  out.close();
}

static int32_t usage() {
  std::cerr << KNOB_BASE::StringKnobSummary() << "\n";
  return 1;
}

int main(int argc, char *argv[]) {
  if (PIN_Init(argc, argv))
    return usage();

  if (InFile.Value().empty()) {
    usage();
    return 1;
  }
  {
    std::ifstream in(InFile.Value());
    ADDRINT addr;
    std::string id;
    while (in >> std::hex >> addr >> id)
      counts[addr].first = id;
    std::cerr << "Parsed " << counts.size() << " entries\n";
  }

  if (OutFile.Value().empty()) {
    usage();
    return 1;
  }
  out.open(OutFile.Value());

  INS_AddInstrumentFunction(InstrumentINS, nullptr);
  PIN_AddFiniFunction(Finish, nullptr);
  std::cerr << "Starting program...\n";
  PIN_StartProgram();
}
