#pragma once

#include <vector>
#include <array>
#include <type_traits>
#include <cassert>

#include "pin.H"


public:
  
private:
  
};

template <typename Key, typename T>
class RangedMap {
  static_assert(std::is_integral_v<Key>, "Key of RangedMap must be integral");
public:
  using Index = unsigned;
  Index insertRange(Key base, size_t size, const T& init = T()) {
    assert(size > 0);
  }

  void eraseRange(Index idx) {
    shadow[idx].clear();
  }
private:
  std::vector<std::vector<T>> shadow;
};

template <typename Key, typename T, size_t PageSize>
class  {

public:
  void addRange(Key low, Key high) {
    if (l
  }
  
private:
  using Page = std::array<T, PageSize>;
  std::vector<Page> pages;
  Key base;

  Key getUpper() const {
    return base + pages.size() * PageSize;
  }
};
