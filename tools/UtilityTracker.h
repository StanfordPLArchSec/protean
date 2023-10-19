#pragma once

#include <iomanip>
#include <fstream>

#include "Util.h"

template <size_t Size, size_t UtilMax, size_t Threshold>
class UtilityTracker {
public:

  struct InstEntry {
    bool valid;
    bool used;
    Addr inst;

    InstEntry(): valid(false) {}
    InstEntry(Addr inst): valid(true), used(false), inst(inst) {}
  };

  class UtilEntry {
  public:
    UtilEntry(): utility(0) {}

    void inc() {
      if (utility < UtilMax)
	++utility;
      utility = UtilMax;
    }

    void dec() {
      if (utility > 0)
	--utility;
    }

    bool hasUtility() const {
      return utility >= Threshold;
    }

    auto getUtility() const {
      return utility;
    }
    
  private:
    size_t utility;
  };


  size_t getInstIndex(Addr addr) const {
    return addr & (inst_table.size() - 1);
  }

  size_t getUtilIndex(Addr inst) const {
    return inst & (util_table.size() - 1);
  }

  InstEntry& getInstEntry(Addr addr) {
    return inst_table[getInstIndex(addr)];
  }

  UtilEntry& getUtilEntry(Addr inst) {
    return util_table[getUtilIndex(inst)];
  }

  
  void handleCheckDeclassified(Addr addr) {
    InstEntry& inst_entry = getInstEntry(addr);
    if (inst_entry.valid) {
      getUtilEntry(inst_entry.inst).inc();
      inst_entry.used = true;
    }
  }

  bool handleSetDeclassified(Addr addr, Addr inst) {
    InstEntry& inst_entry = getInstEntry(addr);

    // Ignore back-to-back writes by the same instruction.
    if (inst_entry.valid && inst_entry.inst == inst)
      return getUtilEntry(inst).hasUtility();

    if (inst_entry.valid && !inst_entry.used) {
      getUtilEntry(inst_entry.inst).dec();
    }
    inst_entry = InstEntry(inst);
    return getUtilEntry(inst).hasUtility();
  }

  void handleSetClassified(Addr addr) {
    InstEntry& inst_entry = getInstEntry(addr);
    if (inst_entry.valid) {
      getUtilEntry(inst_entry.inst).dec();
    }
  }

  void printDesc(std::ostream& os) const {
    os << "utility tracker table with size " << Size << ", utility saturation at " << UtilMax << ", and utility threshold " << Threshold << "\n";
  }

  void printStats(std::ostream& os) const {
    // TODO
  }

  void dump(std::ostream&) {
    std::ofstream os(getFilename("utiltab.out"));
    os << "Utility Table:\n";
    os << "pc utility\n";
    for (const InstEntry& inst_entry : inst_table) {
      if (inst_entry.valid) {
	os << std::hex << std::setfill('0') << std::setw(16) << inst_entry.inst << " " << getUtilEntry(inst_entry.inst).getUtility() << "\n";
      } else {
	os << "(invalid)\n";
      }
    }
  }
  
private:
  std::array<InstEntry, Size> inst_table;
  std::array<UtilEntry, Size> util_table;
};


template <class UtilTab, class DeclTab>
class UtilityDeclassificationTable {
public:
  UtilityDeclassificationTable(const std::string& name, const UtilTab& utiltab, const DeclTab& decltab):
    name(name), utiltab(utiltab), decltab(decltab) {}

  bool checkDeclassified(Addr addr, unsigned size) {
    utiltab.handleCheckDeclassified(addr);
    return decltab.checkDeclassified(addr, size);
  }

  void setDeclassified(Addr addr, unsigned size, Addr inst) {
    const bool allocate = utiltab.handleSetDeclassified(addr, inst);
    if (allocate || decltab.contains(addr)) {
      decltab.setDeclassified(addr, size, inst);
    }
  }

  void setClassified(Addr addr, unsigned size, Addr inst) {
    utiltab.handleSetClassified(addr);
    decltab.setClassified(addr, size, inst);
  }

  void printDesc(std::ostream& os) const {
    os << name << ": utility declassification table\n";
    utiltab.printDesc(os);
    decltab.printDesc(os);
  }

  void printStats(std::ostream& os) const {
    utiltab.printStats(os);
    decltab.printStats(os);
  }

  void dump(std::ostream& os) {
    utiltab.dump(os);
    decltab.dump(os);
  }
  
private:
  std::string name;
  UtilTab utiltab;
  DeclTab decltab;
};
