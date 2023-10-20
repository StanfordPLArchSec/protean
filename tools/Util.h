#pragma once

#include <string>
#include <cassert>
#include <cstdio>
#include "pin.H"

using Addr = ADDRINT;

template <typename InputIt>
std::string bv_to_string8(InputIt first, InputIt last) {
  assert((last - first) % 8 == 0);
  std::string out;
  while (first != last) {
    uint8_t value = 0;
    for (int i = 0; i < 8; ++i) {
      value <<= 1;
      if (*first++)
	value |= 1;
    }
    char buf[16];
    sprintf(buf, "%02hhx", value);
    out += buf;
  }
  return out;
}

template <class Range>
std::string bv_to_string8(const Range& range) {
  return bv_to_string8(range.begin(), range.end());
}

template <typename InputIt>
std::string bv_to_string1(InputIt first, InputIt last) {
  std::string out;
  for (InputIt it = first; it != last; ++it) {
    out += *it ? "1" : "0";
  }
  return out;
}

template <class Range>
std::string bv_to_string1(const Range& range) {
  return bv_to_string1(range.begin(), range.end());
}


std::string getFilename(const std::string& s);


template <typename InputIt, typename OutputIt>
bool hasPattern(InputIt bv_first, InputIt bv_last, OutputIt pattern_out, unsigned MaxPatternLength) {
  const unsigned bv_size = bv_last - bv_first;
  const unsigned max_pattern_length = std::min<unsigned>(MaxPatternLength, bv_size);
  for (unsigned i = 1; i <= max_pattern_length; ++i) {
    bool matched = true;
    for (unsigned j = i; j < bv_size; j += i) {
      const unsigned len = std::min<unsigned>(i, bv_size - j);
      if (!std::equal(bv_first, bv_first + len,
		      bv_first + j, bv_first + j + len)) {
	matched = false;
	break;
      }
    }
    if (matched) {
      std::copy(bv_first, bv_first + i, pattern_out);
      return true;
    }
  }
  return false;
}


template <typename InputIt>
size_t popcnt(InputIt bv_first, InputIt bv_last) {
  return std::count(bv_first, bv_last, true);
}

template <class Range>
size_t popcnt(const Range& range) {
  return popcnt(range.begin(), range.end());
}


namespace util {

  template <size_t Value>
  constexpr inline size_t log2() {
    static_assert(Value > 0, "");
    static_assert((Value & (Value - 1)) == 0, "");
    if constexpr (Value == 1) {
      return 0;
    } else {
      return 1 + log2<Value / 2>();
    }
  }

}

  
