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

template <typename InputIt>
std::string bv_to_string1(InputIt first, InputIt last) {
  std::string out;
  for (InputIt it = first; it != last; ++it) {
    out += *it ? "1" : "0";
  }
  return out;
}


std::string getFilename(const std::string& s);
