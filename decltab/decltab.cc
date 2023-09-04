#include "decltab.hh"

#include <limits>
#include <cstdlib>

// ============= BYTE DECLASSIFCIATION TABLE =========== //

SizedDeclTab::SizedDeclTab(size_t num_entries) {
  if (num_entries == 0) {
    this->numEntries = std::numeric_limits<decltype(this->numEntries)>::max();
  } else {
    this->numEntries = num_entries;
  }
}

bool ByteDeclTab::checkDeclassified(Addr base, unsigned size) {
  for (Addr addr = base; addr < base + size; ++addr) {
    if (!checkDeclassified(addr))
      return false;
  }
  return true;
}

void ByteDeclTab::setDeclassified(Addr base, unsigned size) {
  for (Addr addr = base; addr < base + size; ++addr) {
    setDeclassified(addr);
  }
}

void ByteDeclTab::setClassified(Addr base, unsigned size) {
  for (Addr addr = base; addr < base + size; ++addr) {
    setClassified(addr);
  }
}

// =========== LRU BYTE DECLASSIFICATION TABLE ====== //

bool LRUByteDeclTab::moveToFront(Addr addr) {
  const auto mem_it = mem.find(addr);
  if (mem_it == mem.end())
    return false;
  auto& lru_it = mem_it->second;
  lru.erase(lru_it);
  lru.push_front(addr);
  lru_it = lru.begin();
  return true;
}

bool LRUByteDeclTab::checkDeclassified(Addr addr) {
  return moveToFront(addr);
}

void LRUByteDeclTab::eraseEntry(Map::iterator mem_it) {
  const auto lru_it = mem_it->second;
  lru.erase(lru_it);
  mem.erase(mem_it);
}

void LRUByteDeclTab::evictEntry() {
  assert(!lru.empty());
  eraseEntry(mem.find(lru.back()));
}

void LRUByteDeclTab::setDeclassified(Addr addr) {
#if 0
  // NOTE: Disabling for now since it might make performance worse -- something to check.
  if (moveToFront(addr))
    return;
#else
  if (mem.contains(addr))
    return;
#endif
  if (lru.size() == numEntries)
    evictEntry();
  lru.push_front(addr);  
  mem[addr] = lru.begin();
}

void LRUByteDeclTab::setClassified(Addr addr) {
  const auto mem_it = mem.find(addr);
  if (mem_it == mem.end())
    return;
  eraseEntry(mem_it);
}

// ======== RANDOM BYTE DECLASSIFICATION TABLE ====== //

bool RandomByteDeclTab::checkDeclassified(Addr addr) {
  return set.contains(addr);
}

void RandomByteDeclTab::setDeclassified(Addr addr) {
  if (set.size() == numEntries)
    evictEntry();
  set.insert(addr);
}

void RandomByteDeclTab::setClassified(Addr addr) {
  set.erase(addr);
}

void RandomByteDeclTab::evictEntry() {
  const size_t index = std::rand() % set.size();
  set.erase(std::next(set.begin(), index));
}

// ======== SIZE-AWARE DECLASSIFICATION TABLE ======== //

bool SizeAwareDeclTab::checkDeclassified(Addr base, unsigned size) {
  if (!aligned(base, size))
    return false;
  const Entry entry = {.base = base, .size = size};
  return mem.contains(entry);
}

bool SizeAwareDeclTab::aligned(Addr base, unsigned size) const {
  assert((size & (size - 1)) == 0);
  return (base & (size - 1)) == 0;
}

bool SizeAwareDeclTab::overlap(const Entry& a, const Entry& b) const {
  if (b.base <= a.base && a.base < b.base + b.size)
    return true;
  if (a.base <= b.base && b.base < a.base + a.size)
    return true;
  return false;
}

void SizeAwareDeclTab::setDeclassified(Addr base, unsigned size) {
  if (!aligned(base, size))
    return;
  const Entry entry = {.base = base, .size = size};
  mem.insert(entry);
}

void SizeAwareDeclTab::setClassified(Addr base, unsigned size) {
  for (unsigned check_size = 1; check_size <= 8; check_size <<= 1) {
    for (Addr check_addr = base & ~(check_size - 1);
	 check_addr < base + size;
	 check_addr += check_size) {
      assert(check_addr + check_size >= base);
      const Entry entry = {.base = check_addr, .size = check_size};
      mem.erase(entry);
    }
  }
}


// ========== CONVERT-SIZE DECLASSIFICATION TABLE ========== //

ConvertSizeDeclTab::ConvertSizeDeclTab(size_t access_size, std::unique_ptr<UnsizedDeclTab>&& decltab):
  accessSize(access_size), decltab(std::move(decltab)) {}

bool ConvertSizeDeclTab::checkDeclassified(Addr base, unsigned size) {
  if (size < accessSize)
    return false;
  for (Addr addr = align_up(base); addr + accessSize <= base + size; addr += accessSize)
    if (!decltab->checkDeclassified(addr))
      return false;
  return true;
}

void ConvertSizeDeclTab::setDeclassified(Addr base, unsigned size) {
  if (size >= accessSize)
    for (Addr addr = align_up(base); addr + accessSize <= base + size; addr += accessSize)
      decltab->setDeclassified(addr);
}

void ConvertSizeDeclTab::setClassified(Addr base, unsigned size) {
  if (size >= accessSize)
    for (Addr addr = align_down(base); addr < base + size; addr += accessSize)
      decltab->setClassified(addr);
}

Addr ConvertSizeDeclTab::align_down(Addr addr) const {
  return addr & ~(accessSize - 1);
}

Addr ConvertSizeDeclTab::align_up(Addr addr) const {
  return ((addr - 1) | (accessSize - 1)) + 1;
}


// ======= UNSIZED CACHE DECLASSIFICATION TABLE ======= //

CacheDeclTab::Line CacheDeclTab::newCacheLine() const {
  Line line;
  line.tag = 0;
  line.data.resize(lineSize, false);
  return line;
}

CacheDeclTab::CacheDeclTab(unsigned line_size, unsigned num_lines): lineSize(line_size), numLines(num_lines) {
  cache.resize(numLines, newCacheLine());
}

CacheDeclTab::Index CacheDeclTab::getIndex(Addr addr) const {
  Index idx;
  idx.tag = addr & ~(lineSize - 1);
  idx.table_idx = (addr / lineSize) & (numLines - 1);
  idx.line_idx = addr & (lineSize - 1);
  return idx;
}

bool CacheDeclTab::checkDeclassified(Addr addr) {
  const auto [tag, table_idx, line_idx] = getIndex(addr);
  const Line& line = cache.at(table_idx);
  return line.tag == tag && line.data.at(line_idx);
}

void CacheDeclTab::setDeclassified(Addr addr) {
  const auto [tag, table_idx, line_idx] = getIndex(addr);
  Line& line = cache.at(table_idx);

  if (line.tag != tag) {
    line = newCacheLine();
    line.tag = tag;
  }
  
  line.data.at(line_idx) = true;
}

void CacheDeclTab::setClassified(Addr addr) {
  const auto [tag, table_idx, line_idx] = getIndex(addr);
  Line& line = cache.at(table_idx);
  if (line.tag == tag)
    line.data.at(line_idx) = false;
}
