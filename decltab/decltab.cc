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
  for (Addr addr = align_down(base); addr < base + size; addr += accessSize) {
    assert(base <= addr && addr + accessSize <= base + size);    
    if (!decltab->checkDeclassified(addr / accessSize))
      return false;
  }
  return true;
}

void ConvertSizeDeclTab::setDeclassified(Addr base, unsigned size) {
  for (Addr addr = align_up(base); addr + accessSize <= base + size; addr += accessSize) {
    assert(base <= addr && addr + accessSize <= base + size);
    decltab->setDeclassified(addr / accessSize);
  }
}

void ConvertSizeDeclTab::setDeclassified(Addr base, unsigned size, bool allocate) {
  for (Addr addr = align_up(base); addr + accessSize <= base + size; addr += accessSize) {
    assert(base <= addr && addr + accessSize <= base + size);
    decltab->setDeclassified(addr / accessSize, allocate);
  }
}

void ConvertSizeDeclTab::setClassified(Addr base, unsigned size) {
  for (Addr addr = align_down(base); addr < base + size; addr += accessSize)
    decltab->setClassified(addr / accessSize);
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

CacheDeclTab::Row CacheDeclTab::newCacheRow() const {
  return Row(numWays, newCacheLine());
}

CacheDeclTab::CacheDeclTab(unsigned line_size, unsigned num_lines, unsigned num_ways):
  lineSize(line_size), numLines(num_lines), numWays(num_ways) {
  cache.resize(numRows(), newCacheRow());
}

CacheDeclTab::Index CacheDeclTab::getIndex(Addr addr) const {
  Index idx;
  idx.tag = addr & ~(lineSize - 1);
  idx.table_idx = (addr / lineSize) & (numRows() - 1);
  idx.line_idx = addr & (lineSize - 1);
  return idx;
}

bool CacheDeclTab::checkDeclassified(Addr addr) {
  const auto [tag, table_idx, line_idx] = getIndex(addr);
  const Row& row = cache.at(table_idx);
  const auto row_it = std::find_if(row.begin(), row.end(), [&] (const Line& line) -> bool {
    return line.tag == tag;
  });
  return row_it != row.end() && row_it->data.at(line_idx);
}

CacheDeclTab::Row::iterator CacheDeclTab::evictLine(Row& row) {
  return std::next(row.begin(), std::rand() % row.size());
}

void CacheDeclTab::setDeclassified(Addr addr, bool allocate) {
  if (!allocate) {
    const auto [tag, table_idx, line_idx] = getIndex(addr);
    Row& row = cache.at(table_idx);
    const auto row_it = std::find_if(row.begin(), row.end(), [&] (const Line& line) -> bool {
      return line.tag == tag;
    });
    if (row_it == row.end())
      return;
  }
  setDeclassified(addr);
}

void CacheDeclTab::setDeclassified(Addr addr) {
  const auto [tag, table_idx, line_idx] = getIndex(addr);
  Row& row = cache.at(table_idx);
  auto row_it = std::find_if(row.begin(), row.end(), [&] (const Line& line) -> bool {
    return line.tag == tag;
  });
  if (row_it == row.end()) {
    row_it = evictLine(row);
    *row_it = newCacheLine();
    row_it->tag = tag;
  }
  Line& line = *row_it;
  line.data.at(line_idx) = true;
}

void CacheDeclTab::setClassified(Addr addr) {
  const auto [tag, table_idx, line_idx] = getIndex(addr);
  Row& row = cache.at(table_idx);
  const auto row_it = std::find_if(row.begin(), row.end(), [&] (const Line& line) -> bool {
    return line.tag == tag;
  });
  if (row_it == row.end())
    return;
  Line& line = *row_it;
  line.data.at(line_idx) = false;
}

// ======== PARALLEL DECLASSIFICATION TABLE ======== //
bool ParallelDeclTab::checkDeclassified(Addr base, unsigned size) {
  return
    byteDT.checkDeclassified(base, size) ||
    wordDT.checkDeclassified(base, size) ||
    dwordDT.checkDeclassified(base, size) ||
    qwordDT.checkDeclassified(base, size);
}

void ParallelDeclTab::setDeclassified(Addr base, unsigned size) {
  static const std::vector<std::pair<unsigned, ConvertSizeDeclTab ParallelDeclTab::*>> dts = {
    {1, &ParallelDeclTab::byteDT},
    {2, &ParallelDeclTab::wordDT},
    {4, &ParallelDeclTab::dwordDT},
    {8, &ParallelDeclTab::qwordDT},
  };
  for (const auto [dt_size, dt_memb] : dts) {
    ConvertSizeDeclTab& dt = this->*dt_memb;
    dt.setDeclassified(base, size, size == dt_size);
  }
}

void ParallelDeclTab::setClassified(Addr base, unsigned size) {
  byteDT.setClassified(base, size);
  wordDT.setClassified(base, size);
  dwordDT.setClassified(base, size);
  qwordDT.setClassified(base, size);
}

// ======== HETEROGENOUS CACHE DECLASSIFICATION TABLE ====== //

HeteroCacheDeclTab::HeteroCacheDeclTab(unsigned line_size, unsigned num_lines, unsigned num_ways):
  lineSize(line_size), numWays(num_ways) {
  numRows = num_lines / num_ways;
  cache = Cache(numRows);
}

bool HeteroCacheDeclTab::isAligned(Addr addr, unsigned size) {
  return (addr & (size - 1)) == 0;
}

HeteroCacheDeclTab::Index HeteroCacheDeclTab::getIndex(Addr addr, unsigned orig_scale, unsigned line_scale, bool tight) {
  assert(isAligned(addr, orig_scale));
  Index index;  

  // compute tag
  index.tag.base = addr / (line_scale * lineSize);
  index.tag.scale = line_scale;

  // compute row
  const unsigned rowIdx = index.tag.base & (numRows - 1);
  index.row = &cache.at(rowIdx);

  // compute access indices
  if (tight) {
    index.dataIdxBegin = div_up<Addr>(addr, line_scale) - index.tag.base * lineSize;
    index.dataIdxEnd = div_down<Addr>(addr + orig_scale, line_scale) - index.tag.base * lineSize;
    assert(index.dataIdxEnd <= lineSize);
  } else {
    index.dataIdxBegin = div_down<Addr>(addr, line_scale) - index.tag.base * lineSize;
    index.dataIdxEnd = div_up<Addr>(addr + orig_scale, line_scale) - index.tag.base * lineSize;
    assert(index.dataIdxBegin < index.dataIdxEnd);
    assert(index.dataIdxEnd <= lineSize);
  }

  // try to find cache line, if present
  const auto it = index.row->find(index.tag);
  if (it == index.row->end()) {
    index.data = nullptr;
  } else {
    index.data = &it->second;
  }
  
  return index;
}

bool HeteroCacheDeclTab::checkDeclassifiedOnce(Addr base, unsigned orig_size, unsigned check_size) {
  const Index index = getIndex(base, orig_size, check_size, /*tight*/false);
  if (!index.data)
    return false;
  for (unsigned dataIdx = index.dataIdxBegin; dataIdx < index.dataIdxEnd; ++dataIdx) {
    if (!index.data->at(dataIdx))
      return false;
  }
  return true;
}

bool HeteroCacheDeclTab::checkDeclassified(Addr base, unsigned orig_size) {
  if (!isAligned(base, orig_size)) {
    return
      checkDeclassified(base, orig_size / 2) &&
      checkDeclassified(base + orig_size / 2, orig_size / 2);
  }
  
  for (unsigned check_size : this->check_sizes)
    if (checkDeclassifiedOnce(base, orig_size, check_size))
      return true;
  return false;
}

void HeteroCacheDeclTab::setDeclassified(Addr base, unsigned orig_size) {
  if (!isAligned(base, orig_size)) {
    setDeclassified(base, orig_size / 2);
    setDeclassified(base + orig_size / 2, orig_size / 2);
    return;
  }
  
  for (unsigned check_size : check_sizes) {
    Index index = getIndex(base, orig_size, check_size, /*tight*/true);
    if (!index.data && check_size == orig_size)
      evictAndAllocate(index);
    if (index.data) {
      for (unsigned dataIdx = index.dataIdxBegin; dataIdx < index.dataIdxEnd; ++dataIdx) {
	index.data->at(dataIdx) = true;
      }
    }
  }
}

void HeteroCacheDeclTab::setClassified(Addr base, unsigned orig_size) {
  if (!isAligned(base, orig_size)) {
    setClassified(base, orig_size / 2);
    setClassified(base + orig_size / 2, orig_size / 2);
    return;
  }
  
  for (unsigned check_size : check_sizes) {
    Index index = getIndex(base, orig_size, check_size, /*tight*/false);
    if (index.data) {
      for (unsigned dataIdx = index.dataIdxBegin; dataIdx < index.dataIdxEnd; ++dataIdx) {
	index.data->at(dataIdx) = false;
      }
    }
  }
}

void HeteroCacheDeclTab::evictAndAllocate(Index& index) {
  assert(!index.data);
  Row& row = *index.row;
  if (row.size() == numWays) {
    // Evict
    // Find which row has the lowest occupancy rate.
    Tag min_tag;
    unsigned min_val = std::numeric_limits<unsigned>::max();
    for (const auto& [tag, data] : row) {
      const unsigned val = std::count(data.begin(), data.end(), true);
      if (val < min_val) {
	min_tag = tag;
	min_val = val;
      }
    }
    row.erase(min_tag);
  }
  auto& data = row[index.tag] = std::vector<bool>(lineSize, false);
  index.data = &data;
}
