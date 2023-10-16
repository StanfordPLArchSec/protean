#pragma once

#include <cassert>
#include <string>

template <class DeclTab>
class SharedMultiGranularityDeclassificationTable {
public:
  SharedMultiGranularityDeclassificationTable(const std::string& name, DeclTab& decltab): name(name), decltab(decltab) {}

  static void encodeForDeclassification(ADDRINT& addr, unsigned& size) {
    assert((size & (size - 1)) == 0);
    assert((addr & (size - 1)) == 0);
    assert(size <= 64);
    const unsigned granularity = std::min(size, 8U);

    addr &= (static_cast<ADDRINT>(1) << 48) - 1; // Mask out sign extension; we will use these upper bits for other stuff.
    addr /= granularity; // Shift out the least-significant bits.
    size /= granularity;
    addr |= static_cast<ADDRINT>(granularity) << 48; // Place the granularity in the upper bits.
  }

  template <unsigned granularity>
  static void encodeForClassification(ADDRINT& addr, unsigned& size) {
    static_assert((granularity & (granularity - 1)) == 0, "");
    static_assert(1 <= granularity && granularity <= 8, "");
    assert((addr & (size - 1)) == 0);

    addr &= (static_cast<ADDRINT>(1) << 48) - 1;
    addr /= granularity;
    size = std::max(size / granularity, 1U);
    addr |= static_cast<ADDRINT>(granularity) << 48;
  }

  bool checkDeclassified(ADDRINT addr, unsigned size) {
    encodeForDeclassification(addr, size);
    return decltab.checkDeclassified(addr, size);
  }

  void setDeclassified(ADDRINT addr, unsigned size) {
    encodeForDeclassification(addr, size);
    decltab.setDeclassified(addr, size);
  }

private:
  template <unsigned granularity>
  void setClassified_(ADDRINT addr, unsigned size, ADDRINT store_inst) {
    encodeForClassification<granularity>(addr, size);
    decltab.setClassified(addr, size, store_inst);
  }
  
public:
  void setClassified(ADDRINT addr, unsigned size, ADDRINT store_inst) {
    setClassified_<1>(addr, size, store_inst);
    setClassified_<2>(addr, size, store_inst);
    setClassified_<4>(addr, size, store_inst);
    setClassified_<8>(addr, size, store_inst);
  }

  void printDesc(std::ostream& os) {
    os << name << ": shared multi-granularity declassification table with the following subtable\n";
    decltab.printDesc(os);
  }

  void printStats(std::ostream& os) {
    decltab.printStats(os);
  }

  void dumpTaint(std::vector<uint8_t>&) {}

  void dump(std::ostream& os) {
    decltab.dump(os);
  }
  
private:
  std::string name;
  DeclTab& decltab;
};
