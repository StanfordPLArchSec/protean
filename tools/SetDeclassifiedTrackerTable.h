#pragma once

#include "Util.h"
#include "ShadowMemory.h"

class SetDeclassifiedTrackerTable {
public:
  SetDeclassifiedTrackerTable(): shadow_taint(false), shadow_new(0) {}

  bool checkDeclassified(Addr addr, unsigned size) {
    bool *taint_ptr = &shadow_taint[addr];
    bool *new_ptr = &shadow_new[addr];
    if (std::reduce(new_ptr, new_ptr + size, false, std::logical_or<bool>())) {
      ++stat_used_declstores;
      std::fill_n(new_ptr, size, false);
    }
    return std::reduce(taint_ptr, taint_ptr + size, true, std::logical_and<bool>());
  }

  void setDeclassified(Addr addr, unsigned size) {
    bool *taint_ptr = &shadow_taint[addr];
    bool *new_ptr = &shadow_new[addr];
    if (std::reduce(new_ptr, new_ptr + size, false, std::logical_or<bool>())) {
      ++stat_unused_declstores;
    }
    std::fill_n(taint_ptr, size, true);
    std::fill_n(new_ptr, size, true);
  }

  void setClassified(Addr addr, unsigned size, Addr store_inst) {
    bool *taint_ptr = &shadow_taint[addr];
    bool *new_ptr = &shadow_new[addr];
    if (std::reduce(new_ptr, new_ptr + size, false, std::logical_or<bool>())) {
      ++stat_unused_declstores;
    }
    std::fill_n(taint_ptr, size, false);
    std::fill_n(new_ptr, size, false);
  }

  void printDesc(std::ostream& os) {
    os << "set declassified tracker table\n";
  }

  void printStats(std::ostream& os) {
    // Update unused declstores
    // To be conservative, count and divide by 8.
    unsigned long extra_unused_declstores = 0;
    shadow_new.for_each([&] (Addr addr, bool value) {
      if (value)
	++extra_unused_declstores;
    });
    stat_unused_declstores += extra_unused_declstores / 8;
    
    os << "used-declstores " << stat_used_declstores << "\n"
       << "unused-declstores " << stat_unused_declstores << "\n"
      ;
  }

  void dump(std::ostream&) {}

  void dumpTaint(auto&) {}
  
private:
  ShadowMemory<bool, 0, 12> shadow_taint;
  ShadowMemory<bool, 0, 12> shadow_new;
  unsigned long stat_used_declstores = 0;
  unsigned long stat_unused_declstores = 0;
};
