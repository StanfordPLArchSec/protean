#pragma once

#include <list>
#include <map>
#include <cstddef>
#include <cassert>
#include <set>
#include <memory>
#include <vector>

using Addr = unsigned long;

class DeclTab {
public:
  virtual bool checkDeclassified(Addr base, unsigned size) = 0;
  virtual void setDeclassified(Addr base, unsigned size) = 0;
  virtual void setDeclassified(Addr base, unsigned size, bool allocate) { setDeclassified(base, size); }
  virtual void setClassified(Addr base, unsigned size) = 0;
protected:
private:
};

class UnsizedDeclTab {
public:
  virtual bool checkDeclassified(Addr addr) = 0;
  virtual void setDeclassified(Addr addr) = 0;
  virtual void setDeclassified(Addr addr, bool allocate) { setDeclassified(addr); }
  virtual void setClassified(Addr addr) = 0;
};

class ByteDeclTab : public DeclTab {
public:
  bool checkDeclassified(Addr base, unsigned size) override;
  void setDeclassified(Addr base, unsigned size) override;
  void setClassified(Addr base, unsigned size) override;
protected:
  virtual bool checkDeclassified(Addr addr) = 0;
  virtual void setDeclassified(Addr addr) = 0;
  virtual void setClassified(Addr addr) = 0;
};

class SizedDeclTab {
public:
  SizedDeclTab(size_t num_entries);
protected:
  size_t numEntries;
};

template <typename T>
class LRUStorage {
public:
  LRUStorage(size_t max_entries): maxEntries(max_entries) {}
  
  bool contains(const T& value) {
    const auto mem_it = mem.find(value);
    if (mem_it == mem.end())
      return false;
    auto& lru_it = mem_it->second;
    lru.erase(lru_it);
    lru.push_front(value);
    lru_it = lru.begin();
    return true;
  }
  void insert(const T& value) {
    auto mem_it = mem.find(value);
    if (mem_it == mem.end()) {
      if (maxEntries && mem.size() == maxEntries)
	evict();
      lru.push_front(value);
      mem.emplace(value, lru.begin());
    }
  }
  void erase(const T& value) {
    const auto mem_it = mem.find(value);
    if (mem_it == mem.end())
      return;
    const auto lru_it = mem_it->second;
    lru.erase(lru_it);
    mem.erase(mem_it);
  }
private:
  using List = std::list<T>;
  using Map = std::map<T, typename List::iterator>;
  size_t maxEntries;
  Map mem;
  List lru;

  void evict() {
    assert(!lru.empty());
    erase(lru.back());
  }  
};

template <typename T>
class RandomStorage {
public:
  RandomStorage(size_t max_entries): maxEntries(max_entries) {}

  bool contains(const T& value) {
    return mem.contains(value);
  }
  void insert(const T& value) {
    if (!mem.contains(value)) {
      if (maxEntries && mem.size() == maxEntries)
	evict();
      mem.insert(value);
    }
  }
  void erase(const T& value) {
    mem.erase(value);
  }
protected:
  using Set = std::set<T>;
  size_t maxEntries;
  Set mem;

  void evict() {
    const auto mem_it = std::next(mem.begin(), std::rand() % mem.size());
    mem.erase(mem_it);
  }
};

class LRUByteDeclTab final : public ByteDeclTab, public SizedDeclTab {
public:
  LRUByteDeclTab(size_t num_entries): SizedDeclTab(num_entries) {}
private:
  using List = std::list<Addr>;
  using Map = std::map<Addr, List::iterator>;
  Map mem;
  List lru;
  
  bool checkDeclassified(Addr addr) override;
  void setDeclassified(Addr addr) override;
  void setClassified(Addr addr) override;

  bool moveToFront(Addr addr);
  void evictEntry();
  void eraseEntry(Map::iterator mem_it);
};


template <class DeclTabType>
class AlignedDeclTab final : public DeclTab {
public:
  template <typename... Ts>
  AlignedDeclTab(Ts&&... args): decltab(std::make_unique<DeclTabType>(std::forward<Ts>(args)...)) {}
  
  bool checkDeclassified(Addr base, unsigned size) override {
    if (!aligned(base, size))
      return false;
    return decltab->checkDeclassified(base, size);
  }
  void setDeclassified(Addr base, unsigned size) override {
    if (aligned(base, size))
      decltab->setDeclassified(base, size);
  }
  void setClassified(Addr base, unsigned size) override {
    decltab->setClassified(base, size);
  }
private:
  std::unique_ptr<DeclTab> decltab;
  bool aligned(Addr base, unsigned size) const {
    return (base & (size - 1)) == 0;
  }
};


class RandomByteDeclTab final : public ByteDeclTab, public SizedDeclTab {
public:
  RandomByteDeclTab(size_t num_entries): SizedDeclTab(num_entries) {}
private:
  using Set = std::set<Addr>;
  Set set;
  bool checkDeclassified(Addr addr) override;
  void setDeclassified(Addr addr) override;
  void setClassified(Addr addr) override;
  void evictEntry();
};


class SizeAwareDeclTab final : public DeclTab {
public:
  SizeAwareDeclTab(size_t max_entries): mem(max_entries) {}
  bool checkDeclassified(Addr base, unsigned size) override;
  void setDeclassified(Addr base, unsigned size) override;
  void setClassified(Addr base, unsigned size) override;
private:
  struct Entry {
    Addr base;
    unsigned size;
    auto operator<=>(const Entry&) const = default;
  };
  LRUStorage<Entry> mem;

  bool aligned(Addr base, unsigned size) const;
  bool overlap(const Entry& a, const Entry& b) const;
};

class ConvertSizeDeclTab final : public DeclTab {
public:
  ConvertSizeDeclTab(size_t access_size, std::unique_ptr<UnsizedDeclTab>&& decltab);
  bool checkDeclassified(Addr base, unsigned size) override;
  void setDeclassified(Addr base, unsigned size) override;
  void setDeclassified(Addr base, unsigned size, bool allocate) override;
  void setClassified(Addr base, unsigned size) override;
private:
  size_t accessSize;
  std::unique_ptr<UnsizedDeclTab> decltab;

  Addr align_down(Addr addr) const;
  Addr align_up(Addr addr) const;
};


class ShadowDeclTab final : public UnsizedDeclTab {
public:
  template <typename... Ts>
  ShadowDeclTab(Ts&&... args): mem(std::forward<Ts>(args)...) {}
  
  bool checkDeclassified(Addr addr) override {
    return mem.contains(addr);
  }
  
  void setDeclassified(Addr addr) override {
    mem.insert(addr);
  }
  
  void setClassified(Addr addr) override {
    mem.erase(addr);
  }
  
private:
  LRUStorage<Addr> mem;
};


class CacheDeclTab final : public UnsizedDeclTab {
public:
  CacheDeclTab(unsigned line_size, unsigned num_lines, unsigned num_ways);

  bool checkDeclassified(Addr addr) override;
  void setDeclassified(Addr addr) override;
  void setDeclassified(Addr addr, bool allocate) override;
  void setClassified(Addr addr) override;
  
private:
  unsigned lineSize;
  unsigned numLines;
  unsigned numWays;

  unsigned numRows() const {
    return numLines / numWays;
  }
  
  struct Line {
    Addr tag;
    std::vector<bool> data;
    
    bool contains(Addr addr) const {
      return tag <= addr && addr < tag + data.size();
    }
  };
  Line newCacheLine() const;

  using Row = std::vector<Line>;
  Row newCacheRow() const;
  
  using Cache = std::vector<Row>;
  Cache cache;

  struct Index {
    Addr tag;
    unsigned table_idx;
    unsigned line_idx;
  };
  Index getIndex(Addr addr) const;

  Row::iterator evictLine(Row& row);
};


class ParallelDeclTab final : public DeclTab {
public:
  ParallelDeclTab(std::unique_ptr<UnsizedDeclTab>&& byteDT,
		  std::unique_ptr<UnsizedDeclTab>&& wordDT,
		  std::unique_ptr<UnsizedDeclTab>&& dwordDT,
		  std::unique_ptr<UnsizedDeclTab>&& qwordDT):
    byteDT(1, std::move(byteDT)),
    wordDT(2, std::move(wordDT)),
    dwordDT(4, std::move(dwordDT)),
    qwordDT(8, std::move(qwordDT)) {}

  bool checkDeclassified(Addr base, unsigned size) override;
  void setDeclassified(Addr base, unsigned size) override;
  void setClassified(Addr base, unsigned size) override;
  
private:
  ConvertSizeDeclTab byteDT;
  ConvertSizeDeclTab wordDT;
  ConvertSizeDeclTab dwordDT;
  ConvertSizeDeclTab qwordDT;
};


class HeteroCacheDeclTab final : public DeclTab {
public:
  HeteroCacheDeclTab(unsigned line_size, unsigned num_lines, unsigned num_ways);

  bool checkDeclassified(Addr base, unsigned size) override;
  void setDeclassified(Addr base, unsigned size) override;
  void setClassified(Addr base, unsigned size) override;

private:
  struct Tag {
    Addr base;
    unsigned scale;
    auto operator<=>(const Tag&) const = default;
  };
  using Row = std::map<Tag, std::vector<bool>>;
  using Cache = std::vector<Row>;

  unsigned lineSize;
  unsigned numWays;
  unsigned numRows;  
  Cache cache;

  template <typename T>
  static void assertPow2(T x) {
    assert((x & (x - 1)) == 0);
  }
  static bool isAligned(Addr addr, unsigned size);

  struct Index {
    Row *row; // never null
    Tag tag;
    std::vector<bool> *data;
    unsigned dataIdxBegin;
    unsigned dataIdxEnd;
  };
  Index getIndex(Addr addr, unsigned orig_size, unsigned check_size, bool tight);

  bool checkDeclassifiedOnce(Addr base, unsigned orig_size, unsigned check_size);

  template <typename T>
  static T div_down(T a, T b) {
    return a / b;
  }

  template <typename T>
  static T div_up(T a, T b) {
    return (a + (b - 1)) / b;
  }

  void evictAndAllocate(Index& index);

  static inline const std::array<unsigned, 4> check_sizes = {1, 2, 4, 8};
};
