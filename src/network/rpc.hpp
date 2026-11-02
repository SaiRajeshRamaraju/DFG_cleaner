#pragma once
#include <cstdint>
#include <iostream>
#pragma pack(push, 1)
struct MessageHeader {
  uint32_t magic;
  uint8_t msg_type;
  uint64_t payload_len;
};
#pragma pack(pop)
