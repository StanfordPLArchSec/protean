#pragma once

#include <cstdint>

#define AC_OFFSET 0
#define AC_MASK 0x1
#define AC_READ 0x0
#define AC_WRITE 0x1

#define TT_OFFSET 1
#define TT_MASK 0x2
#define TT_CLASSIFY 0x0
#define TT_DECLASSIFY 0x2

struct __attribute__((__packed__)) Record {
  void *inst;
  void *addr;
  uint8_t mode;
  
  bool isRead() {
    return (mode & AC_MASK) == AC_READ;
  }

  void setRead() {
    mode &= ~AC_MASK;
    mode |= AC_READ;
  }

  bool isWrite() {
    return (mode & AC_MASK) == AC_WRITE;
  }

  void setWrite() {
    mode &= ~AC_MASK;
    mode |= AC_WRITE;
  }

  uint8_t getSize() {
    return mode >> 2;
  }

  void setSize(uint8_t size) {
    mode |= size << 2;
  }
};
