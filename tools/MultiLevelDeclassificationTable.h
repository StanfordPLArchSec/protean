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
    bool downgrade;
    Addr downgrade_addr;
    std::array<bool, LowerDT::LineSize> downgrade_bv;
    if (upper.setDeclassified(addr, size, downgrade, downgrade_addr, downgrade_bv))
      return;
    assert(!downgrade || (downgrade_addr & (LowerDT::LineSize - 1)) == 0);
    
    bool evicted;
    Addr evicted_addr;
    std::array<bool, LowerDT::LineSize> evicted_bv;    

    if (downgrade) {
      lower.downgradeLine(downgrade_addr, downgrade_bv, evicted, evicted_addr, evicted_bv);
      if (evicted) {
	assert((evicted_addr & (LowerDT::LineSize - 1)) == 0);
	upper.claimLine(evicted_addr, evicted_bv);
      }
    }
    
    lower.setDeclassified(addr, size, evicted, evicted_addr, evicted_bv);
    if (evicted) {
      assert((evicted_addr & (LowerDT::LineSize - 1)) == 0);
      const bool claimed = upper.claimLine(evicted_addr, evicted_bv);
      if (claimed)
	++stat_upper_claims;
    }
  }

  void setClassified(Addr addr, unsigned size, Addr store_inst) {
    bool downgrade;
    Addr downgrade_addr;
    std::array<bool, LowerDT::LineSize> downgrade_bv;
    upper.setClassified(addr, size, store_inst, downgrade, downgrade_addr, downgrade_bv);
    if (downgrade) {
      bool evicted;
      Addr evicted_addr;
      std::array<bool, LowerDT::LineSize> evicted_bv;
      lower.downgradeLine(downgrade_addr, downgrade_bv, evicted, evicted_addr, evicted_bv);
      if (evicted) {
	const bool claimed = upper.claimLine(evicted_addr, evicted_bv);
	if (claimed)
	  ++stat_upper_claims;
      }
    }
    lower.setClassified(addr, size, store_inst);
    assert(!upper.checkDeclassified(addr, size) && !lower.checkDeclassified(addr, size));
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
       << "upper-misses " << stat_upper_misses << "\n"
       << "upper-claims " << stat_upper_claims << "\n"
      ;
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
  unsigned long stat_upper_claims = 0;
};
