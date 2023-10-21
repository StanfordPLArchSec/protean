#pragma once

#include "Util.h"

template <size_t QueueSize, size_t Threshold, size_t PageSize, class Decltab>
class ScanQueue {
public:
  ScanQueue(const Decltab& decltab): decltab(decltab) {}

  bool checkDeclassified(Addr addr, unsigned size) {
    return decltab.checkDeclassified(addr, size);
  }

  void setDeclassified(Addr addr, unsigned size, Addr inst) {
    if (decltab.contains(addr)) {
      decltab.setDeclassified(addr, size, inst);
    }

    const auto page_count = std::count_if(queue.begin(), queue.end(), [addr] (Addr other_addr) {
      const Addr page1 = addr / PageSize;
      const Addr page2 = other_addr / PageSize;
      return page1 == page2;
    });
    if (page_count >= Threshold) {
      // don't allocate
      return;
    }
    
    decltab.setDeclassified(addr, size, inst);
    
    if (queue.size() == QueueSize)
      queue.pop_front();
    queue.push_back(addr);
  }

  void setClassified(Addr addr, unsigned size, Addr inst) {
    decltab.setClassified(addr, size, inst);
  }

  void printDesc(std::ostream& os) const {
    os << "scan queue with queue size " << QueueSize << ", threshold " << Threshold << ", and page size " << PageSize << "\n";
    decltab.printDesc(os);
  }
  void printStats(std::ostream& os) const {
    decltab.printStats(os);
  }
  void dump(std::ostream& os) const {
    decltab.dump(os);
  }

private:
  Decltab decltab;
  std::list<Addr> queue;
};
