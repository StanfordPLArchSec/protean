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
  virtual void setClassified(Addr base, unsigned size) = 0;
protected:
private:
};

class UnsizedDeclTab {
public:
  virtual bool checkDeclassified(Addr addr) = 0;
  virtual void setDeclassified(Addr addr) = 0;
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
  CacheDeclTab(unsigned line_size, unsigned num_lines);

  bool checkDeclassified(Addr addr) override;
  void setDeclassified(Addr addr) override;
  void setClassified(Addr addr) override;
  
private:
  unsigned lineSize;
  unsigned numLines;
  struct Line {
    Addr tag;
    std::vector<bool> data;
    
    bool contains(Addr addr) const {
      return tag <= addr && addr < tag + data.size();
    }
  };
  using Cache = std::vector<Line>;
  Cache cache;

  Line newCacheLine() const;

  struct Index {
    Addr tag;
    unsigned table_idx;
    unsigned line_idx;
  };
  Index getIndex(Addr addr) const;
  
};
