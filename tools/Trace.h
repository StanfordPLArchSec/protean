#pragma once

#include <cstdint>

namespace trace {
  enum Tag : uint8_t {
    INST,
    READ,
    WRITE,
  };

  struct __attribute__((packed)) Packet {
    Tag tag;
  };

  struct __attribute__((packed)) InstPacket {
    Tag tag;
    uint64_t pc;
  };

  struct __attribute__((packed)) ReadPacket {
    Tag tag;
    uint64_t ea;
  };

  struct __attribute__((packed)) WritePacket {
    Tag tag;
    uint64_t ea;
  };
  
    
}
