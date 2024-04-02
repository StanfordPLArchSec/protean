#include <unistd.h>
#include <iostream>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>
#include <cassert>
#include <optional>
#include <unordered_map>
#include <err.h>
#include <set>

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> tokens;
  std::string_view view(s);
  while (!view.empty()) {
    auto next_idx = view.find_first_of(sep);
    if (next_idx == view.npos)
      next_idx = view.size();
    tokens.emplace_back(view.data(), next_idx);
    if (next_idx == view.size())
      break;
    view.remove_prefix(next_idx + 1);
  }
  return tokens;
}

std::string_view strip(std::string_view s) {
  while (!s.empty() && std::isspace(s.front()))
    s.remove_prefix(1);
  while (!s.empty() && std::isspace(s.back()))
    s.remove_suffix(1);
  return s;
}

std::string strip(const std::string& s) {
  return std::string(strip(std::string_view(s)));
}

template <class InputIt>
std::string join(InputIt first, InputIt last, char sep) {
  std::string s;
  for (InputIt it = first; it != last; ++it) {
    if (it != first)
      s += sep;
    s += *it;
  }
  return s;
}

static void usage(FILE *f, const char *prog) {
  fprintf(f, "usage: %s [-h]\n", prog);
}

using Cycle = unsigned long;
using SeqNum = unsigned long;
using Addr = unsigned long;

enum  MicroOperand {
  OP_IMM,
  OP_MEM,
  OP_REG_IN,
  OP_REG_OUT,
  OP_JUNK,
};

static std::string parse_err;

template <class OutputIt1, class OutputIt2, class Operands>
bool parse_microop_operands(std::string_view s, const Operands& ops, OutputIt1 ins, OutputIt2 outs) {
  for (auto op_it = ops.begin(); op_it != ops.end(); ++op_it) {
    const MicroOperand op = *op_it;
    // Strip leading whitespace.
    while (!s.empty() && std::isspace(s.front()))
      s.remove_prefix(1);
    if (s.empty()) {
      parse_err = "missing operand";
      return false;
    }

    // Find end of operand.
    const auto end = (std::next(op_it) == ops.end()) ? s.size() : s.find(',');
    std::string_view opstr(s.data(), end);
    opstr = strip(opstr);
    s.remove_prefix(end);
    if (!s.empty() && s.front() == ',')
      s.remove_prefix(1);

    // Parse operand.
    switch (op) {
    case OP_IMM: break;
    case OP_JUNK: break;
    case OP_MEM:
      {
	auto open = opstr.find('[');
	if (open == opstr.npos) {
	  parse_err = "no opening '['";
	  return false;
	}
	opstr.remove_prefix(open + 1);
	const auto close = opstr.find(']');
	if (close == opstr.npos) {
	  parse_err = "no closing ']'";
	  return false;
	}
	opstr.remove_suffix(opstr.size() - close);
	const auto tokens = split(std::string(opstr), '+');
	for (const auto& token : tokens) {
	  char *end;
	  std::strtoul(token.c_str(), &end, 0);
	  if (*end) {
	    // Failed to parse, so treat as register.
	    *ins++ = strip(token);
	  }
	}
	break;
      }
    case OP_REG_IN:
      *ins++ = std::string(opstr);
      break;
    case OP_REG_OUT:
      *outs++ = std::string(opstr);
      break;
    }
  }

  s = strip(s);
  if (!s.empty()) {
    parse_err = std::string("stray text at end of line: ") + std::string(s);
    return false;
  }
  
  return true;
}

template <class OutputIt1, class OutputIt2>
bool parse_microop(std::string_view disasm, OutputIt1 ins, OutputIt2 outs) {
  if (disasm.find("(unimplemented)") != disasm.npos ||
      disasm.find(".mfence") != disasm.npos
      )
    return true;
  
  // Get opcode
  while (!disasm.empty() && std::isspace(disasm.front()))
    disasm.remove_prefix(1);
  if (disasm.empty())
    return false;
  const auto opcode_end = disasm.find(' ');
  const std::string_view opcode(disasm.data(), opcode_end == disasm.npos ? disasm.size() : opcode_end);
  disasm.remove_prefix(opcode_end == disasm.npos ? disasm.size() : opcode_end + 1);

  // Get operands
  static const std::unordered_map<std::string, std::vector<MicroOperand>> map = {
    {"NOP", {}},
    {"limm", {OP_REG_OUT, OP_IMM}},
    {"syscall", {}},
    {"ld", {OP_REG_OUT, OP_MEM}},
    {"ldfp", {OP_REG_OUT, OP_MEM}},
    {"ldifp87", {OP_REG_OUT, OP_MEM}},
    {"ldfp87", {OP_REG_OUT, OP_MEM}},
    {"ldst", {OP_REG_OUT, OP_MEM}},
    {"ldstl", {OP_REG_OUT, OP_MEM}},
    {"st", {OP_REG_IN, OP_MEM}},
    {"stul", {OP_REG_IN, OP_MEM}},
    {"stfp", {OP_REG_IN, OP_MEM}},    
    {"fault", {OP_JUNK}},
    {"add", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"and", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"srl", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"sld", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"sub", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"rcr", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"rcl", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"wrdh", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"sbb", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"rol", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"ror", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"adc", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"xor", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"srd", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"sll", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"or", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"rdip", {OP_REG_OUT, OP_REG_OUT}},
    {"wrip", {OP_REG_IN, OP_REG_IN}},
    {"wripi", {OP_REG_IN, OP_REG_IN}},
    {"zexti", {OP_REG_OUT, OP_REG_IN, OP_IMM}},
    {"sexti", {OP_REG_OUT, OP_REG_IN, OP_IMM}},
    {"xori", {OP_REG_OUT, OP_REG_IN, OP_IMM}},
    {"zext", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"sext", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"movi", {OP_REG_OUT, OP_JUNK, OP_IMM}},
    {"addi", {OP_REG_OUT, OP_REG_IN, OP_IMM}},
    {"subi", {OP_REG_OUT, OP_REG_IN, OP_IMM}},
    {"rcli", {OP_REG_OUT, OP_REG_IN, OP_IMM}},
    {"srdi", {OP_REG_OUT, OP_REG_IN, OP_IMM}},
    {"andi", {OP_REG_OUT, OP_REG_IN, OP_IMM}},
    {"roli", {OP_REG_OUT, OP_REG_IN, OP_IMM}},
    {"srli", {OP_REG_OUT, OP_REG_IN, OP_IMM}}, 
    {"rcri", {OP_REG_OUT, OP_REG_IN, OP_IMM}},
    {"rori", {OP_REG_OUT, OP_REG_IN, OP_IMM}},
    {"slli", {OP_REG_OUT, OP_REG_IN, OP_IMM}},
    {"srai", {OP_REG_OUT, OP_REG_IN, OP_IMM}},
    {"div1", {OP_REG_IN, OP_REG_IN}},
    {"div2", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"div2i", {OP_REG_OUT, OP_REG_IN, OP_IMM}},
    {"mov", {OP_REG_OUT, OP_JUNK, OP_REG_IN}},
    {"lea", {OP_REG_OUT, OP_MEM}},
    {"CPUID", {}},
    {"mov2fp", {OP_REG_OUT, OP_REG_IN, OP_IMM}},
    {"lfpimm", {OP_REG_OUT, OP_IMM}},
    {"unpack", {OP_REG_OUT, OP_REG_OUT, OP_REG_IN}},
    {"br", {OP_IMM}},
    {"divq", {OP_REG_OUT, OP_JUNK}},
    {"divr", {OP_REG_OUT, OP_JUNK}},
    {"mul1s", {OP_REG_IN, OP_REG_IN}},
    {"mul1u", {OP_REG_IN, OP_REG_IN}},
    {"mul1si", {OP_REG_IN, OP_IMM}},
    {"mulel", {OP_REG_OUT, OP_JUNK}},
    {"muleh", {OP_REG_OUT, OP_JUNK}},
    {"movfp", {OP_REG_OUT, OP_REG_IN}},
    {"mxor", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"msubf", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"maddi", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"maddf", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"mdivf", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"mminf", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"mand", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"mdbi", {OP_REG_OUT, OP_IMM}},
    {"mmulf", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"shuffle", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"cvti2f", {OP_REG_OUT, OP_REG_IN}},
    {"msrli", {OP_REG_OUT, OP_REG_IN, OP_IMM}},
    {"mcmpi2r", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"mcmpf2rf", {OP_REG_IN, OP_REG_OUT}},
    {"XCHG_M_R.mfence", {}},
    {"CMPXCHG_LOCKED_M_R.mfence", {}},
    {"IN_R_I.mfence", {}},
    {"inst_ib", {OP_JUNK}},
    {"cvtf2i", {OP_REG_OUT, OP_REG_IN}},
    {"mov2int", {OP_REG_OUT, OP_REG_IN, OP_IMM}},
    {"rdm5reg", {OP_REG_OUT, OP_REG_OUT}},
    {"panic", {OP_JUNK, OP_JUNK}},
    {"divfp", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"addfp", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"IN_R_R.mfence", {}},
    {"ruflags", {OP_REG_OUT, OP_REG_OUT}},
    {"ruflag", {OP_REG_OUT, OP_IMM}},
    {"wruflags", {OP_REG_IN, OP_REG_IN}},
    {"eret", {}},
    {"rdsel", {OP_REG_OUT, OP_JUNK}},
    {"OUTS_R_M.mfence", {}},
    {"OUT_I_R.mfence", {}},
    {"chks", {OP_REG_OUT, OP_REG_IN, OP_IMM}},
    {"fiadd", {OP_JUNK}},
    {"wrsel", {OP_JUNK, OP_REG_IN}},
    {"msubi", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"wrdl", {OP_JUNK, OP_REG_IN, OP_REG_IN}},
    {"mor", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"wrflags", {OP_REG_IN, OP_REG_IN}},
    {"sra", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"movsign", {OP_REG_OUT, OP_REG_IN}},
    {"rflag", {OP_REG_OUT, OP_IMM}},
    {"HLT", {}},
    {"rdlimit", {OP_REG_OUT, OP_JUNK}},
    {"ori", {OP_REG_OUT, OP_REG_IN, OP_IMM}},
    {"fidivr", {OP_JUNK}},
    {"rdval", {OP_REG_OUT, OP_JUNK}},
    {"fcom", {OP_JUNK}},
    {"fxam", {OP_JUNK}},
    {"Unknown", {OP_JUNK}},
    {"fwait", {OP_JUNK}},
    {"compfp", {OP_REG_IN, OP_REG_IN}},
    {"cvtint_fp80", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"absfp", {OP_REG_OUT, OP_REG_IN}},
    {"cosfp", {OP_REG_OUT, OP_REG_IN}},
    {"mulfp", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"subfp", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"cda", {OP_MEM}},
    {"rflags", {OP_REG_OUT, OP_REG_OUT}},
    {"wruflagsi", {OP_REG_IN, OP_IMM}},
    {"mcmpf2r", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"mmaxf", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"rdattr", {OP_REG_OUT, OP_JUNK}},
    {"rdcr", {OP_REG_OUT, OP_JUNK}},
    {"wrval", {OP_REG_OUT, OP_REG_IN}},
    {"cvtf_d2i", {OP_REG_OUT, OP_REG_IN}},
    {"pop87", {}},
    {"MOV", {}},
    {"pack", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"mandn", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"mavg", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"msad", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"mmuli", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"stfp87", {OP_REG_IN, OP_MEM}},
    {"cvtf2f", {OP_REG_OUT, OP_REG_IN}},
    {"mslli", {OP_REG_OUT, OP_REG_IN, OP_IMM}},
    {"palignr", {OP_REG_OUT, OP_REG_OUT, OP_REG_IN, OP_REG_IN, OP_IMM}},
    {"CLTS", {}},
    {"msrai", {OP_REG_OUT, OP_REG_IN, OP_IMM}},
    {"msrl", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"rounds", {OP_REG_OUT, OP_REG_IN, OP_REG_IN, OP_IMM}},
    {"SYSRET_TO_COMPAT", {}},
    {"mmaxi", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"msll", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"msra", {OP_REG_OUT, OP_REG_IN, OP_REG_IN}},
    {"msqrt", {OP_REG_OUT, OP_REG_IN}},
    {"cvtfp80h_int", {OP_REG_OUT, OP_REG_IN}},
    {"cvtfp80l_int", {OP_REG_OUT, OP_REG_IN}},
    {"rdtsc", {OP_REG_OUT, OP_REG_OUT}},
    {"gem5Op", {}},
    {"LFENCE", {}},
  };

  const auto it = map.find(std::string(opcode));
  if (it == map.end()) {
    parse_err = "unhandled opcode";
    return false;
  }

  if (!parse_microop_operands(disasm, it->second, ins, outs))
    return false;

  return true;
}

struct Inst {
  Addr addr;
  SeqNum seq_num;
  unsigned microop_idx;
  bool issued;
  bool completed;
  bool retired;
  std::string disasm;
  bool declmiss;
  bool cfisrc;
  bool cfidst;
};

int main(int argc, char *argv[]) {
  char optc;
  while ((optc = getopt(argc, argv, "h")) >= 0) {
    switch (optc) {
    case 'h':
      usage(stdout, argv[0]);
      return 0;
    default:
      usage(stderr, argv[0]);
      return 1;
    }
  }

  std::istream& in = std::cin;

  std::vector<Inst> insts;

  // NOTE: We only care about *transiently* *executed* instructions.
  // *transient* means they didn't retire.
  // *executed* means they reached the "complete" stage.
  in.sync_with_stdio(false);
  in.tie(nullptr);
  
  std::string fetch_line;
  std::string llsct_line;
  std::string decode_line;
  std::string rename_line;
  std::string dispatch_line;
  std::string issue_line;
  std::string complete_line;
  std::string retire_line;
  while (std::getline(in, fetch_line) &&
	 std::getline(in, llsct_line) &&
	 std::getline(in, decode_line) &&
	 std::getline(in, rename_line) &&
	 std::getline(in, dispatch_line) &&
	 std::getline(in, issue_line) &&
	 std::getline(in, complete_line) &&
	 std::getline(in, retire_line)
	 ) {
    Inst inst;

    // O3PipeView:fetch:80500:0x00401026:1:40:  ADD_M_R : add   t1b, t1b, al
    const auto fetch_tokens = split(fetch_line, ':');
    assert(fetch_tokens.at(0) == "O3PipeView" && fetch_tokens.at(1) == "fetch");
    inst.addr = std::stoul(fetch_tokens.at(3), nullptr, 0);
    inst.seq_num = std::stoul(fetch_tokens.at(5));
    inst.microop_idx = std::stoul(fetch_tokens.at(4));
    inst.disasm = join(fetch_tokens.begin() + 6, fetch_tokens.end(), ':');
    if (const auto pos = inst.disasm.find(" : "); pos != inst.disasm.npos)
      inst.disasm = inst.disasm.substr(pos + 3);

    // O3PipeView:llsct:na:na
    const auto llsct_tokens = split(llsct_line, ':'); 
    assert(llsct_tokens.at(1) == "llsct");
    inst.declmiss = (llsct_tokens.at(2) == "miss");
    inst.cfisrc = (llsct_tokens.at(3) == "src");
    inst.cfidst = (llsct_tokens.at(3) == "dst");

    // O3PipeView:issue:80500
    inst.issued = (std::stoul(split(issue_line, ':')[2]) > 0);

    // O3PipeView:complete:81000
    inst.completed = (std::stoul(split(complete_line, ':')[2]) > 0);

    // O3PipeView:retire:82500:store:0
    inst.retired = (std::stoul(split(retire_line, ':')[2]) > 0);

    if (!inst.retired && inst.issued)
      insts.push_back(inst);

    std::vector<std::string> ins, outs;
    if (!parse_microop(inst.disasm, std::back_inserter(ins), std::back_inserter(outs)))
      errx(1, "failed to parse microop: %s: %s", inst.disasm.c_str(), parse_err.c_str());
    
  }

  std::sort(insts.begin(), insts.end(), [] (const Inst& a, const Inst& b) -> bool {
    return a.seq_num < b.seq_num;
  });

  std::cout << "parsed " << insts.size() << " insts\n";
#if 0
  for (const Inst& inst : insts) {
    std::vector<std::string> ins, outs;
    if (!parse_microop(inst.disasm, std::back_inserter(ins), std::back_inserter(outs)))
      errx(1, "failed to parse microop: %s: %s", inst.disasm.c_str(), parse_err.c_str());
    std::cout << inst.seq_num << " " << std::hex << inst.addr << " " << std::dec << inst.disasm;
#if 0
    std::cout << " ins";
    for (const auto& in : ins)
      std::cout << " " << in;
    std::cout << " outs";
    for (const auto& out : outs)
      std::cout << " " << out;
#endif
    std::cout << "\n";
  }
#endif

  // Check instructions.
  SeqNum last_seq_num = 0;
  bool ok = true;
  std::set<std::string> tainted_regs;
  unsigned long declmiss_count = 0;
  unsigned long phantom_jumps = 0;
  for (auto it = insts.begin(); it != insts.end(); ++it) {
    if (it->seq_num != last_seq_num + 1) {
      assert(last_seq_num < it->seq_num);
      tainted_regs.clear();
    }

    // HACK: Filter out phantom jumps.
    long disp;
    if (std::next(it) - insts.begin() >= 3 && std::next(it) != insts.end() &&
	std::prev(it, 2)->disasm == "rdip   t1, t1" &&
	std::sscanf(std::prev(it, 1)->disasm.c_str(), "limm   t2, 0x%lx", &disp) == 1 &&
	std::prev(it, 0)->disasm == "wrip   t1, t2") {
      if (it->addr + disp != std::next(it)->addr) {
	++phantom_jumps;
	// std::cerr << "Skipping phantom jump: " << it->seq_num << " " << std::hex << it->addr << std::dec << "\n";
	continue;
      }
    }

    last_seq_num = it->seq_num;

    const Inst& inst = *it;
    std::vector<std::string> ins, outs;
    if (!parse_microop(inst.disasm, std::back_inserter(ins), std::back_inserter(outs)))
      errx(1, "failed to parse microop: %s", inst.disasm.c_str());

    // Check if any tainted registers are used in an instruction that was issued.
    if (inst.issued) {
      for (const auto& in : ins) {
	if (tainted_regs.contains(in)) {
	  std::cout << "VIOLATION: seqnum " << inst.seq_num << " instruction " << std::hex << inst.addr << std::dec << " executed with tainted "
		    << "register " << in << "\n";
#if 1
	  ok = false;
#else
	  return 1;
#endif
	}
      }
    }

    // Update taint register file.
    for (const auto& reg : outs) {
      if (inst.declmiss) {
	++declmiss_count;
	tainted_regs.insert(reg);
      } else {
	tainted_regs.erase(reg);
      }
    }
    
  }

  std::cout << "total declmisses " << declmiss_count << "\n";
  std::cout << "total phantom-jmps " << phantom_jumps << "\n";

  return ok ? 0 : 1;
}
