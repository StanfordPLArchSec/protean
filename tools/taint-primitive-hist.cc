#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <cstring>
#include <sstream>
#include <vector>
#include <cassert>
#include <err.h>
#include <fstream>
#include <iostream>
#include <fstream>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <regex>
#include <cstdint>

using Addr = uint64_t;

struct Info {
  unsigned long reg = 0;
  unsigned long mem = 0;
  unsigned long xmit = 0;
};

enum TaintPrimitive {REG_TAINT, MEM_TAINT, XMIT_TAINT};


static void usage(std::ostream &os) {
  os << "usage: taint-primitive-hist dbgout.txt.gz...\n";
}

int main(int argc, char *argv[]) {
  using namespace std;
  
  if (argc < 2) {
    usage(std::cerr);
    return EXIT_FAILURE;
  }

  std::unordered_map<Addr, Info> map;

  for (int argi = 1; argi < argc; ++argi) {
    std::ifstream f(argv[1], std::ios_base::in | std::ios_base::binary);
    boost::iostreams::filtering_istream in;
    in.push(boost::iostreams::gzip_decompressor());
    in.push(f);

    // 118447316060000: system.switch_cpus: TPE m-taint 0x3c763a ::   MOV_R_M : ld   r13, DS:[rax] :: srcs={invalid[0] rax miscellaneous[155]} dests={r13}
    std::string line;
    std::regex re(" +");
    while (std::getline(in, line)) {
      std::sregex_token_iterator it(line.begin(), line.end(), re, -1);
      std::vector<std::string> tokens(it, {});
      std::erase_if(tokens, [] (const std::string &s) { return s.empty(); }); // Might not need this.
      if (tokens.size() < 5 ||
          tokens[2] != "TPE")
        continue;
      const Addr inst_addr = std::stoul(tokens[4]);
      Info &record = map[inst_addr];
      const std::string &type = tokens[3];
      if (type == "r-taint") {
        ++record.reg;
      } else if (type == "m-taint") {
        ++record.mem;
      } else if (type == "x-taint") {
        ++record.xmit;
      }
    }
  }

  std::vector<std::tuple<unsigned long, TaintPrimitive, Addr>> vec;
  for (const auto &[inst_addr, record] : map) {
    if (record.reg)
      vec.emplace_back(record.reg, REG_TAINT, inst_addr);
    if (record.mem)
      vec.emplace_back(record.mem, MEM_TAINT, inst_addr);
    if (record.xmit)
      vec.emplace_back(record.xmit, XMIT_TAINT, inst_addr);
  }

  std::sort(vec.begin(), vec.end(), [] (const auto &a, const auto &b) {
    return std::get<0>(a) > std::get<0>(b);
  });

  for (const auto &t : vec) {
    std::cout << std::get<0>(t) << " " << std::hex << std::get<2>(t) << " ";
    char c = '\0';
    switch (std::get<1>(t)) {
    case REG_TAINT: c = 'r'; break;
    case MEM_TAINT: c = 'm'; break;
    case XMIT_TAINT: c = 'x'; break;
    default: std::abort();
    }
    std::cout << c << "\n";
  }
}
