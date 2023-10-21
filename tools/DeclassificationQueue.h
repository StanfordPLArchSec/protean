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


  bool checkDeclassified(Addr addr, unsigned size, int& upgrade, Addr& upgrade_addr, std::array<bool, LineSize>& upgrade_bv) {
    upgrade = 0;
    for (auto it = queue.begin(); it != queue.end(); ++it) {
      if (it->contains(addr)) {
	if (it->allSet(addr, size)) {
	  it->hits++;
	  if (it->hits == Threshold) {
	    // Promote this line.
	    upgrade = 1;
	    upgrade_addr = it->baseaddr();
	    upgrade_bv = it->bv;
	  } else {
	    // Move to the end of the queue.
	    queue.push_back(*it);
	  }
	  queue.erase(it);
	  return true;
	} else {
	  return false;
	}
      }
    }
    return false;
  }

  void setDeclassified(Addr addr, unsigned size, int& upgrade, Addr& upgrade_addr, std::array<bool, LineSize>& upgrade_bv) {
    upgrade = 0;
    
    for (auto it = queue.begin(); it != queue.end(); ++it) {
      // Only count if it's not already set.
      if (it->contains(addr)) {
	if (!it->allSet(addr, size)) {
	  it->setRange(addr, size);
	  queue.push_back(*it);
	  queue.erase(it);
	}
	return;
      }
    }

    // Otherwise, add new entry to queue.
    // But first check if queue is full.
    if (queue.size() == QueueSize) {
      const Entry ent = queue.front();
      queue.pop_front();

      auto it = queue.begin();
      size_t matches = 1;
      constexpr size_t scan_threshold = 8;
      for (; it != queue.end() && matches < scan_threshold; ++it) {
	if (it->tag == ent.tag + matches)
	  ++matches;
      }

      if (matches < scan_threshold) {
	upgrade = 1;
      } else {
#if 0
	upgrade = 2;
#endif
      }
      
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

  void validate() const {
#ifndef NDEBUG
    std::set<Addr> tags;
    for (const Entry& entry : queue) {
      const bool inserted = tags.insert(entry.tag).second;
      assert(inserted);
    }
#endif
  }

  void printDesc(std::ostream& os) const {
    os << name << ": declassification queue with line size " << LineSize << " and queue size " << QueueSize << "\n";
  }

  void printStats(std::ostream& os) const {
  }

  void dump(std::ostream& os) const {
  }

  DeclassificationQueue(const std::string& name): name(name) {}

private:
  std::string name;
  std::list<Entry> queue;
};


template <class Queue, class Cache1, class Cache2>
class QueuedDeclassificationCache {
public:
  QueuedDeclassificationCache(const std::string& name, const Queue& queue, const Cache1& cache1, const Cache2& cache2):
    name(name), queue(queue), cache1(cache1), cache2(cache2)
  {
  }

  bool checkDeclassified(Addr addr, unsigned size) {
    if (cache1.contains(addr)) {
      return cache1.checkDeclassified(addr, size);
    } else if (cache2.contains(addr)) {
      return cache2.checkDeclassified(addr, size);
    }
    
    int upgrade;
    Addr upgrade_addr;
    std::array<bool, Queue::LineSize> upgrade_bv;
    if (!queue.checkDeclassified(addr, size, upgrade, upgrade_addr, upgrade_bv))
      return false;
    queue.validate();
    switch (upgrade) {
    case 0:
      break;
    case 1:
      cache1.claimLine(upgrade_addr, upgrade_bv);
      break;
    case 2:
      cache2.claimLine(upgrade_addr, upgrade_bv);
      break;
    }
    return true;
  }

  void setDeclassified(Addr addr, unsigned size, Addr inst) {
    if (cache1.contains(addr)) {
      cache1.setDeclassified(addr, size, inst);
    } else if (cache2.contains(addr)) {
      cache2.setDeclassified(addr, size, inst);
    } else {
      int upgrade;
      Addr upgrade_addr;
      std::array<bool, Queue::LineSize> upgrade_bv;
      queue.setDeclassified(addr, size, upgrade, upgrade_addr, upgrade_bv);
      queue.validate();
      switch (upgrade) {
      case 0:
	break;
      case 1:
	cache1.claimLine(upgrade_addr, upgrade_bv);
	break;
      case 2:
	cache2.claimLine(upgrade_addr, upgrade_bv);
	break;
      }
    }
  }

  void setClassified(Addr addr, unsigned size, Addr store_inst) {
    if (cache1.contains(addr)) {
      cache1.setClassified(addr, size, store_inst);
    } else if (cache2.contains(addr)) {
      cache2.setClassified(addr, size, store_inst);
    } else {
      queue.setClassified(addr, size);
      queue.validate();
    }
  }

  void printDesc(std::ostream& os) const {
    os << name << ": queued declassification table\n";
    queue.printDesc(os);
    cache1.printDesc(os);
    cache2.printDesc(os);
  }

  void printStats(std::ostream& os) const {
    queue.printStats(os);
    cache1.printStats(os);
    cache2.printStats(os);
  }

  void dump(std::ostream& os) const {
    queue.dump(os);
    {
      std::ofstream f1(getFilename("cache1.out"));
      cache1.dump(f1);
    }
    {
      std::ofstream f2(getFilename("cache2.out"));
      cache2.dump(f2);
    }
  }
  
private:
  std::string name;
  Queue queue;
  Cache1 cache1;
  Cache2 cache2;
};

