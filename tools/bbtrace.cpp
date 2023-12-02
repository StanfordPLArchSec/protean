#include <fstream>
#include <iostream>
#include <map>
#include "pin.H"

static KNOB<std::string> OutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "specify output BBV file");
static KNOB<unsigned long> IntervalSize(KNOB_MODE_WRITEONCE, "pintool", "interval-size", "0", "specify interval size");

struct Block {
  unsigned long id;
  unsigned long size;
  unsigned long hits;

  Block(unsigned long id, unsigned long size): id(id), size(size), hits(0) {
    assert(id > 0);
    reset();
  }
  
  void reset() {
    hits = 0;
  }
};

static std::ofstream out;
static unsigned long interval_size;
static unsigned long inst_count;
static std::map<ADDRINT, Block> blocks;

static void DumpInterval() {
  out << "T";
  for (auto& [_, block] : blocks) {
    if (block.hits != 0)
      out << " :" << block.id << ":" << (block.hits * block.size);
    block.reset();
  }
  out << std::endl;
  inst_count = 0;
}

static void HandleBlock(Block *block) {
  ++block->hits;
  inst_count += block->size;
  if (inst_count >= interval_size) {
    // Interval reached.
    DumpInterval();
  }
}

static void Trace(TRACE trace, void *) {
  for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
    const ADDRINT addr = BBL_Address(bbl);
    auto block_it = blocks.find(addr);
    if (block_it == blocks.end()) {
      const auto result = blocks.emplace(addr, Block(blocks.size() + 1, BBL_NumIns(bbl)));
      assert(result.second);
      block_it = result.first;
    }
    assert(block_it != blocks.end());
    Block *block = &block_it->second;
    assert(block->size == BBL_Size(bbl));
    BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR) HandleBlock, IARG_ADDRINT, block, IARG_END);
  }
}

static void Fini(int32_t code, void *) {
  out.close();
}

static int32_t usage() {
  PIN_ERROR("bbtrace: basic block profiler for SimPoints\n");
  std::cerr << KNOB_BASE::StringKnobSummary() << "\n";
  return 1;
}

int main(int argc, char *argv[]) {
  if (PIN_Init(argc, argv))
    return usage();

  if (OutputFile.Value().empty()) {
    std::cerr << "pinpoints: -o: required\n";
    return 1;
  }
  out.open(OutputFile.Value());

  if (IntervalSize.Value() == 0) {
    std::cerr << "pinpoints: -interval: required\n";
    return 1;
  }
  interval_size = IntervalSize.Value();

  TRACE_AddInstrumentFunction(Trace, nullptr);
  PIN_AddFiniFunction(Fini, nullptr);
  PIN_StartProgram();
}
