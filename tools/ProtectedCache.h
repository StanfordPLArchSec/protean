#pragma once

template <class Decltab1, class Decltab2>
class ProtectedDeclassificationTable {
public:

  static inline constexpr size_t popcnt_threshold = 2;
  
  ProtectedDeclassificationTable(const Decltab1& decltab1, const Decltab2& decltab2):
    decltab1(decltab1), decltab2(decltab2) {}

  bool checkDeclassified(Addr addr, unsigned size) {
    return decltab1.checkDeclassified(addr, size) || decltab2.checkDeclassified(addr, size);
  }

  template <class BV>
  bool keepLine(const BV& bv) const {
    return popcnt(bv) >= popcnt_threshold;
  }

  void setDeclassified(Addr addr, unsigned size, Addr inst) {
    if (decltab2.contains(addr)) {
      decltab2.setDeclassified(addr, size, inst);
      return;
    }

    bool evicted;
    Addr evicted_addr;
    std::array<bool, Decltab1::LineSize> evicted_bv;
    decltab1.setDeclassified(addr, size, evicted, evicted_addr, evicted_bv);
    if (!evicted)
      return;

    // Is this worth passing up to the protected table?
    if (!keepLine(evicted_bv))
      return;

    decltab2.claimLine(evicted_addr, evicted_bv);
  }

  void setClassified(Addr addr, unsigned size, Addr inst) {
    decltab1.setClassified(addr, size, inst);
    decltab2.setClassified(addr, size, inst);
  }

  void printDesc(std::ostream& os) const {
    decltab1.printDesc(os);
    decltab2.printDesc(os);
  }

  void printStats(std::ostream& os) const {
    decltab1.printStats(os);
    decltab2.printStats(os);
  }

  void dump(std::ostream& os) const {
    decltab1.dump(os);
    decltab2.dump(os);
  }
    
  
private:
  Decltab1 decltab1;
  Decltab2 decltab2;
};
