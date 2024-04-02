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


template <size_t Size>
class UtilityTracker2 {
public:
  struct InstEntry {
    Addr inst;
    InstEntry(): inst(0) {}
    InstEntry(Addr inst): inst(inst) {}
  };

  struct UtilEntry {
    bool used;
    UtilEntry(): used(false) {}
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
    getUtilEntry(getInstEntry(addr).inst).used = true;
  }

  bool handleSetDeclassified(Addr addr, Addr inst) {
    getInstEntry(addr).inst = inst;
    const bool allocate = getUtilEntry(inst).used;
    if (allocate) {
      ++stat_allocates;
    } else {
      ++stat_noallocates;
    }
    return allocate;
  }

  void handleSetClassified(Addr addr) {
    getInstEntry(addr).inst = 0;
  }

  void printDesc(std::ostream& os) const {
    os << "simple utility tracker bitmask\n";
  }

  void printStats(std::ostream& os) const {
    os << "allocates " << stat_allocates << "\n"
       << "noallocates " << stat_noallocates << "\n";
  }

  void dump(std::ostream&) {
    std::ofstream os(getFilename("utiltab.out"));
    os << "pc used\n";
    for (const InstEntry& inst_entry : inst_table) {
      os << std::hex << std::setw(16) << std::setfill('0') << inst_entry.inst << " " << getUtilEntry(inst_entry.inst).used << "\n";
    }
  }
  
private:
  std::array<InstEntry, Size> inst_table;
  std::array<UtilEntry, Size> util_table;
  unsigned long stat_allocates = 0;
  unsigned long stat_noallocates = 0;
};


class Average {
public:
  float value() const {
    if (samples == 0) {
      return 0.f;
    } else {
      return sum / samples;
    }
  }

  void add(float x) {
    sum += x;
    ++samples;
  }

  void reset() {
    sum = 0.f;
    samples = 0;
  }
  
private:
  float sum = 0.f;
public:
  unsigned long samples = 0;
};


template <size_t Size>
class UtilityTracker3 {
public:
  struct InstEntry {
    Addr inst;
    InstEntry(): inst(0) {}
    InstEntry(Addr inst): inst(inst) {}
  };

  using Tick = unsigned long;

  struct UtilEntry {
    // Instance stats.
    Addr inst = 0;
    
    Tick alloc_tick = 0;
    Tick first_use_tick = 0;
    Tick last_use_tick = 0;
    unsigned long hits = 0;

    // Aggregate stats
    Average used_avg;
    Average hit_avg;
    Average ttu_avg;
    Average lifetime_avg;

    void startLifetime(Addr new_inst, Tick tick) {
      if (inst != new_inst) {
	used_avg.reset();
	hit_avg.reset();
	ttu_avg.reset();
	lifetime_avg.reset();
      }
      inst = new_inst;
      alloc_tick = tick;
      first_use_tick = 0;
      last_use_tick = 0;
      hits = 0;
    }

    void endLifetime() {
      if (inst && alloc_tick) {
	used_avg.add(first_use_tick ? 1 : 0);
	if (first_use_tick) {
	  ttu_avg.add(first_use_tick - alloc_tick);
	  lifetime_avg.add(last_use_tick - alloc_tick);
	}
	hit_avg.add(hits);
      }
      hits = alloc_tick = first_use_tick = last_use_tick = 0;
    }

    void dump(std::ostream& os) const {
      if (inst == 0)
	return;
      char buf[1024];
      sprintf(buf, "%016lx %.2f %.2f %.2f %.2f\n",
	      inst, used_avg.value(), hit_avg.value(), ttu_avg.value(), lifetime_avg.value());
      os << buf;
    }
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
    ++tick;
    UtilEntry& util_entry = getUtilEntry(getInstEntry(addr).inst);
    if (util_entry.first_use_tick == 0)
      util_entry.first_use_tick = tick;
    util_entry.last_use_tick = tick;
    ++util_entry.hits;
  }

  bool handleSetDeclassified(Addr addr, Addr inst) {
    ++tick;
    getInstEntry(addr).inst = inst;
    UtilEntry& util_entry = getUtilEntry(inst);
    util_entry.endLifetime();
    util_entry.startLifetime(inst, tick);
    return util_entry.hit_avg.samples < 16 || util_entry.hit_avg.value() * util_entry.used_avg.value() > 0.01;
  }

  void handleSetClassified(Addr addr) {
    ++tick;
    getInstEntry(addr).inst = 0;
    getUtilEntry(getInstEntry(addr).inst).endLifetime();
  }

  void printDesc(std::ostream& os) {}
  void printStats(std::ostream& os) {}
  void dump(std::ostream&) {
    std::ofstream os(getFilename("utiltab.out"));
    for (const UtilEntry& util_entry : util_table) {
      util_entry.dump(os);
    }
  }
  
private:
  std::array<InstEntry, Size> inst_table;
  std::array<UtilEntry, Size> util_table;
  Tick tick = 0;
};







template <size_t Size>
class UtilityTracker4 {
public:
  struct AccEntry {
    Addr inst = 0;
    bool used = false;
  };
  struct InstEntry {
    Addr inst = 0;
    Average used_avg;
  };

  size_t getAccIndex(Addr addr) const { return addr & (acc_table.size() - 1); }
  size_t getInstIndex(Addr inst) const { return inst & (inst_table.size() - 1); }
  AccEntry& getAccEntry(Addr addr) { return acc_table[getAccIndex(addr)]; }
  InstEntry& getInstEntry(Addr inst) { return inst_table[getInstIndex(inst)]; }
  
  void handleCheckDeclassified(Addr addr) {
    AccEntry& acc_entry = getAccEntry(addr);
    if (!acc_entry.used) {
      InstEntry& inst_entry = getInstEntry(acc_entry.inst);
      inst_entry.used_avg.add(1);
      acc_entry.used = true;
    }
  }

  bool handleSetDeclassified(Addr addr, Addr inst) {
    AccEntry& acc_entry = getAccEntry(addr);

    if (!acc_entry.used) {
      InstEntry& inst_entry = getInstEntry(acc_entry.inst);
      inst_entry.used_avg.add(0);
    }

    acc_entry.inst = inst;
    acc_entry.used = false;
    InstEntry& inst_entry = getInstEntry(inst);
    if (inst_entry.inst != inst) {
      inst_entry.inst = inst;
      inst_entry.used_avg.reset();
    }

    if (inst_entry.used_avg.samples < 16)
      return true;
    
#if 0
    const float randval = static_cast<float>(std::rand()) / RAND_MAX;
    return randval / 2 < inst_entry.used_avg.value();
#endif
    return inst_entry.used_avg.value() > 0.01;
  }

  void handleSetClassified(Addr addr) {
    AccEntry &acc_entry = getAccEntry(addr);
    if (!acc_entry.used) {
      getInstEntry(acc_entry.inst).used_avg.add(0);
    }
    acc_entry.inst = 0;
    acc_entry.used = false;
  }

  void printDesc(std::ostream& os) const {}
  void printStats(std::ostream& os) const {
  }
  void dump(std::ostream& os) const {
    for (const InstEntry& inst_entry : inst_table) {
      char buf[1024];
      sprintf(buf, "%016lx %.2f\n", inst_entry.inst, inst_entry.used_avg.value());
      os << buf;
    }
  }
  
private:
  std::array<AccEntry, Size> acc_table;
  std::array<InstEntry, Size> inst_table;
};




template <size_t LineSize, size_t Size>
class UtilityTracker5 {
public:
  struct DynamicLine {
    Addr tag;
    size_t stc_idx;

    DynamicLine(Addr addr, size_t stc_idx): tag(getTag(addr)), stc_idx(stc_idx) {}

    Addr getTag(Addr addr) const { return addr / LineSize; }
    Addr baseaddr() const { return tag * LineSize; }
    bool contains(Addr addr) const { return tag == getTag(addr); }
  };

  struct StaticLine {
    size_t checks = 0;
    size_t sets = 0;

    void dump(std::ostream& os) const {
      os << checks << " " << sets << "\n";
    }
  };

  struct Inst {
    size_t stc_idx;

    Inst(size_t stc_idx): stc_idx(stc_idx) {}
  };

  size_t getDynLineIndex(Addr addr) const {
    return (addr / LineSize) & (dyn_lines.size() - 1);
  }
  std::optional<DynamicLine>& getDynLine(Addr addr) {
    return dyn_lines[getDynLineIndex(addr)];
  }
  size_t getInstIndex(Addr inst) const { return inst & (insts.size() - 1); }
  std::optional<Inst>& getInst(Addr inst) { return insts[getInstIndex(inst)]; }


  void handleCheckDeclassified(Addr addr) {
    std::optional<DynamicLine>& dyn_line = getDynLine(addr);
    if (!(dyn_line && dyn_line->contains(addr)))
      return;
    std::optional<StaticLine>& stc_line = stc_lines[dyn_line->stc_idx];
    if (!stc_line)
      return;
    stc_line->checks++;
  }

  bool handleSetDeclassified(Addr addr, Addr inst_addr) {
    std::optional<DynamicLine>& dyn_line = getDynLine(addr);
    std::optional<Inst>& inst = getInst(inst_addr);

    if (dyn_line && !dyn_line->contains(addr))
      dyn_line = std::nullopt;

    if (!dyn_line && !inst) {
      inst = Inst(std::rand() % stc_lines.size());
      std::optional<StaticLine>& stc_line = stc_lines[inst->stc_idx];
      stc_line = StaticLine();
    } else if (!dyn_line && inst) {
      dyn_line = DynamicLine(addr, inst->stc_idx);
    } else if (dyn_line && !inst) {
      inst = Inst(dyn_line->stc_idx);
    } else if (dyn_line->stc_idx != inst->stc_idx) {
      inst->stc_idx = dyn_line->stc_idx;
    }
    assert(inst->stc_idx == dyn_line->stc_idx);

    StaticLine& stc_line = *stc_lines[dyn_line->stc_idx];
    ++stc_line.sets;

    return stc_line.sets >= stc_line.checks;
  }
    
  void handleSetClassified(Addr addr) {
    std::optional<DynamicLine>& dyn_line = getDynLine(addr);
    if (dyn_line) {
      StaticLine& stc_line = *stc_lines[dyn_line->stc_idx];
#if 0
      stc_line.sets = std::max<size_t>(1, stc_line.sets) - 1;
#endif
    }
  }

  void printDesc(std::ostream& os) const {}
  void printStats(std::ostream& os) const {}
  void dump(std::ostream& os) const {
    for (const auto& stc_line : stc_lines) {
      if (stc_line)
	stc_line->dump(os);
    }
  }

private:
  std::array<std::optional<DynamicLine>, Size> dyn_lines;
  std::array<std::optional<Inst>, Size> insts;
  std::array<std::optional<StaticLine>, Size> stc_lines;
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

  void printDesc(std::ostream& os) {
    os << name << ": utility declassification table\n";
    utiltab.printDesc(os);
    decltab.printDesc(os);
  }

  void printStats(std::ostream& os) {
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
