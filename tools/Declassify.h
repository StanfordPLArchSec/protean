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

using static_id_t = uint16_t;

#if 0
enum AccessType : uint8_t {
  AC_LOAD,
  AC_STORE,
};

enum TaintType : uint8_t {
  TT_DECLASSIFY,
  TT_CLASSIFY,
}

struct StaticAccess {
  uint64_t iaddr; // instruction address
  AccessType ac;
  TaintType tt;
};

struct DynamicAccess {
public:
  DynamicAccess(static_id_t id, uint64_t addr) {
    data = (id << 48) | (addr & data_mask);
  }

  uint64_t getAddr() const {
    return data & data_mask;
  }

  static_id_t getID() const {
    return data >> 48;
  }
  
private:
  uint64_t data;

  static inline constexpr data_mask = (1UL << 48) - 1;
};

struct Header {
  static_id_t num_static;
  uint64_t num_dynamic;
};
#endif

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
