#pragma once

#include "pin.H"
#include <vector>

class VsyscallPage {
public:
  VsyscallPage(): data(size, 0) {
    memcpy(data.data() + vtime_offset, vtime_blob, sizeof vtime_blob);
    memcpy(data.data() + vgettimeofday_offset, vgettimeofday_blob, sizeof vgettimeofday_blob);
  }
  
  bool contains(ADDRINT addr) const {
    return addr >= base && addr < base + size;
  }

  uint8_t *getBasePointer() {
    return data.data();
  }

  uint8_t *getPointer(ADDRINT addr) {
    assert(contains(addr));
    return getBasePointer() + (addr - base);
  }

private:
  static inline constexpr ADDRINT base = 0xffffffffff600000ULL;
  static inline constexpr ADDRINT size = 0x1000;
  static inline constexpr ADDRINT vtime_offset = 0x400;
  static inline constexpr uint8_t vtime_blob[] = {
    0x48,0xc7,0xc0,0xc9,0x00,0x00,0x00,    // mov    $0xc9,%rax
    0x0f,0x05,                             // syscall
    0xc3                                   // retq
  };
  static inline constexpr ADDRINT vgettimeofday_offset = 0x0;
  static inline constexpr uint8_t vgettimeofday_blob[] = {
    0x48,0xc7,0xc0,0x60,0x00,0x00,0x00,    // mov    $0x60,%rax
    0x0f,0x05,                             // syscall
    0xc3                                   // retq
  };
  std::vector<uint8_t> data;
  
};
