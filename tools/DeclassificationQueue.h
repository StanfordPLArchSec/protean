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


  bool checkDeclassified(Addr addr, unsigned size, bool& upgrade, Addr& upgrade_addr, std::array<bool, LineSize>& upgrade_bv) {
    upgrade = false;
    for (auto it = queue.begin(); it != queue.end(); ++it) {
      if (it->contains(addr)) {
	if (it->allSet(addr, size)) {
	  it->hits++;
	  if (it->hits == Threshold) {
	    // Promote this line.
	    upgrade = true;
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

  void setDeclassified(Addr addr, unsigned size) {
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
      queue.pop_front();
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


template <class Queue, class Cache>
class QueuedDeclassificationCache {
public:
  QueuedDeclassificationCache(const std::string& name, const Queue& queue, const Cache& cache):
    name(name), queue(queue), cache(cache)
  {
  }

  bool checkDeclassified(Addr addr, unsigned size) {
    if (cache.contains(addr)) {
      return cache.checkDeclassified(addr, size);
    }
    
    bool upgrade;
    Addr upgrade_addr;
    std::array<bool, Queue::LineSize> upgrade_bv;
    if (!queue.checkDeclassified(addr, size, upgrade, upgrade_addr, upgrade_bv))
      return false;
    queue.validate();
    cache.claimLine(upgrade_addr, upgrade_bv);
    return true;
  }

  void setDeclassified(Addr addr, unsigned size) {
    if (cache.contains(addr)) {
      cache.setDeclassified(addr, size);
    } else {
      queue.setDeclassified(addr, size);
      queue.validate();
    }
  }

  void setClassified(Addr addr, unsigned size, Addr store_inst) {
    if (cache.contains(addr)) {
      cache.setClassified(addr, size, store_inst);
    } else {
      queue.setClassified(addr, size);
      queue.validate();
    }
  }

  void printDesc(std::ostream& os) const {
    os << name << ": queued declassification table\n";
    queue.printDesc(os);
    cache.printDesc(os);
  }

  void printStats(std::ostream& os) const {
    queue.printStats(os);
    cache.printStats(os);
  }

  void dump(std::ostream& os) const {
    queue.dump(os);
    cache.dump(os);
  }
  
private:
  std::string name;
  Queue queue;
  Cache cache;
};
