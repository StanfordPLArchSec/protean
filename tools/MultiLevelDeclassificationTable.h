#pragma once

#include "Util.h"


template <class LowerDT, class UpperDT>
class MultiLevelDeclassificationTable {
public:
  MultiLevelDeclassificationTable(LowerDT& lower, UpperDT& upper): lower(lower), upper(upper) {}

  bool checkDeclassified(Addr addr, unsigned size) {
    if (lower.checkDeclassified(addr, size)) {
      ++stat_lower_hits;
      return true;
    }
    ++stat_lower_misses;
    if (upper.checkDeclassified(addr, size)) {
      ++stat_upper_hits;
      return true;
    }
    ++stat_upper_misses;
    return false;
  }

  void setDeclassified(Addr addr, unsigned size) {
    if (upper.setDeclassified(addr, size))
      return;
    
    bool evicted;
    Addr evicted_addr;
    std::array<bool, LowerDT::LineSize> evicted_bv;
    lower.setDeclassified(addr, size, evicted, evicted_addr, evicted_bv);
    
    if (evicted) {
      upper.claimLine(evicted_addr, evicted_bv);
    }
  }

  void setClassified(Addr addr, unsigned size, Addr store_inst) {
    lower.setClassified(addr, size, store_inst);
    upper.setClassified(addr, size, store_inst);
  }

  void printDesc(std::ostream& os) {
    os << "multi-level declassification table\n";
    os << "Lower level:\n";
    lower.printDesc(os);
    os << "Upper level:\n";
    upper.printDesc(os);
  }

  void printStats(std::ostream& os) {
    os << "lower-hits " << stat_lower_hits << "\n"
       << "lower-misses " << stat_lower_misses << "\n"
       << "upper-hits " << stat_upper_hits << "\n"
       << "upper-misses " << stat_upper_misses << "\n";
    lower.printStats(os);
    upper.printStats(os);
  }

  void dump(std::ostream& os) {
    os << "==== LOWER ====\n";
    lower.dump(os);
    os << "\n==== UPPER ====\n";
    upper.dump(os);
  }

private:
  LowerDT& lower;
  UpperDT& upper;
  unsigned long stat_lower_hits = 0, stat_lower_misses = 0, stat_upper_hits = 0, stat_upper_misses = 0;
};
