#pragma once

#include <list>

#include "pin.H"

#include "Util.h"

template <size_t LineSize_, size_t QueueSize_, size_t Threshold_>
class DeclassificationQueue {
public:
  static inline constexpr size_t LineSize = LineSize_;
  static_assert((LineSize & (LineSize - 1)) == 0, "");
  static inline constexpr size_t QueueSize = QueueSize_;
  static_assert(QueueSize > 0, "");
  static inline constexpr size_t Threshold = Threshold_;

  using BV = std::array<bool, LineSize>;

  struct Entry {
    Addr tag;
    std::array<bool, LineSize> bv;
    unsigned hits;

    static Addr getTag(Addr addr) {
      return addr / LineSize;
    }

    static size_t getIndex(Addr addr) {
      return addr & (LineSize - 1);
    }

    bool contains(Addr addr) const {
      return getTag(addr) == tag;
    }

    Addr baseaddr() const {
      return tag * LineSize;
    }

  private:
    auto getIterator(Addr addr) {
      return bv.begin() + getIndex(addr);
    }
    
  public:

    bool allSet(Addr addr, unsigned size) {
      const auto it = getIterator(addr);
      return std::reduce(it, it + size, true, std::logical_and<bool>());
    }

    void setRange(Addr addr, unsigned size, bool value = true) {
      std::fill_n(getIterator(addr), size, value);
    }

    void resetRange(Addr addr, unsigned size) {
      setRange(addr, size, false);
    }


    Entry(Addr addr, unsigned size): tag(getTag(addr)), hits(0) {
      std::fill(bv.begin(), bv.end(), false);
      std::fill_n(bv.begin() + getIndex(addr), size, true);
    }
  };


  bool checkDeclassified(Addr addr, unsigned size) {
    for (auto it = queue.begin(); it != queue.end(); ++it) {
      if (it->contains(addr)) {
	if (it->allSet(addr, size)) {
	  it->hits++;
	  return false;
	} else {
	  return true;
	}
      }
    }
    return false;
  }

  void setDeclassified(Addr addr, unsigned size,
		       bool& upgrade, Addr& upgrade_addr, std::array<bool, LineSize>& upgrade_bv) {
    upgrade = false;
    
    for (auto it = queue.begin(); it != queue.end(); ++it) {
      // Only count if it's not already set.
      if (it->contains(addr)) {
	it->setRange(addr, size);
	return;
      }
    }

    // Otherwise, add new entry to queue.
    // But first check if queue is full.
    if (queue.size() == QueueSize) {
      const Entry ent = queue.front();
      queue.pop_front();

      upgrade = true;
      upgrade_addr = ent.baseaddr();
      upgrade_bv = ent.bv;
    }
    
    queue.emplace_back(addr, size);
  }

  void setClassified(Addr addr, unsigned size) {
    for (Entry& entry : queue) {
      if (entry.contains(addr))
	entry.resetRange(addr, size);
    }
  }

  void printDesc(std::ostream& os) const {
    os << name << ": declassification queue with line size " << LineSize << " and queue size " << QueueSize << "\n";
  }

  void printStats(std::ostream& os) const {
    os << name << ".discarded " << stat_discarded << "\n";
    os << name << ".upgraded " << stat_upgraded << "\n";
  }

  void dump(std::ostream& os) const {
  }

  DeclassificationQueue(const std::string& name): name(name) {}

private:
  std::string name;
  std::list<Entry> queue;
  unsigned long stat_discarded = 0;
  unsigned long stat_upgraded = 0;
};


template <class Queue, class Decltab>
class QueuedDeclassificationCache {
public:
  QueuedDeclassificationCache(const std::string& name, const Queue& queue, const Decltab& decltab):
    name(name), queue(queue), decltab(decltab) {}

  
  bool checkDeclassified(Addr addr, unsigned size) {
    if (decltab.contains(addr)) {
      return decltab.checkDeclassified(addr, size);
    } else {
      return queue.checkDeclassified(addr, size);
    }
  }

  void setDeclassified(Addr addr, unsigned size, Addr inst) {
    if (decltab.contains(addr)) {
      decltab.setDeclassified(addr, size, inst);
    } else {
      bool upgrade;
      Addr upgrade_addr;
      std::array<bool, Queue::LineSize> upgrade_bv;
      queue.setDeclassified(addr, size, upgrade, upgrade_addr, upgrade_bv);

      if (upgrade) {
	constexpr unsigned long interval = 1000000;
	constexpr unsigned long print_num = 512;
	static unsigned long counter = 0;
	++counter;
	if (counter % interval <= print_num) {
	  if (!queue_file)
	    queue_file = fopen(getFilename("queue.out").c_str(), "w");
	  fprintf(queue_file, "%016lx %s\n", upgrade_addr, bv_to_string8(upgrade_bv).c_str());
	}
	decltab.claimLine(upgrade_addr, upgrade_bv);
      }
    }
  }

  void setClassified(Addr addr, unsigned size, Addr store_inst) {
    if (decltab.contains(addr)) {
      decltab.setClassified(addr, size, store_inst);
    } else {
      queue.setClassified(addr, size);
    }
  }

  void printDesc(std::ostream& os) const {
    os << name << ": queued declassification table\n";
    queue.printDesc(os);
    decltab.printDesc(os);
  }

  void printStats(std::ostream& os) const {
    queue.printStats(os);
    decltab.printDesc(os);
  }

  void dump(std::ostream& os) const {
    queue.dump(os);
    decltab.dump(os);
  }
  
private:
  std::string name;
  Queue queue;
  Decltab decltab;
  FILE *queue_file = nullptr;
};

