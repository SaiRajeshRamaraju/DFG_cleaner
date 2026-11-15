#pragma once

#include <cstdint>
#include <cstring>
#include <string>

constexpr uint32_t DFG_MAGIC = 0xDF610026;

enum class MsgType : uint8_t {
  PING = 0x01,
  PONG = 0x02,
  REGISTER_UPLOAD = 0x10,
  REGISTER_UPLOAD_ACK = 0x11,
  GET_METADATA = 0x12,
  METADATA_RESP = 0x13,
  UPLOAD_CHUNK = 0x20,
  UPLOAD_ACK = 0x21,
  DOWNLOAD_CHUNK = 0x22,
  DOWNLOAD_RESP = 0x23,
  DELETE_FILE = 0x30,
  DELETE_ACK = 0x31,
  ERROR_RESP = 0xFF
};

#pragma pack(push, 1)
struct MessageHeader {
  uint32_t magic;
  uint8_t msg_type;
  uint64_t payload_len;
};
#pragma pack(pop)

static_assert(sizeof(MessageHeader) == 13,
              "MessageHeader must be exactly 13 bytes on the wire");

inline MessageHeader make_header(MsgType type, uint64_t payload_len) {
  MessageHeader hdr{};
  hdr.magic = DFG_MAGIC;
  hdr.msg_type = static_cast<uint8_t>(type);
  hdr.payload_len = payload_len;
  return hdr;
}

inline bool validate_magic(const MessageHeader &hdr) {
  return hdr.magic == DFG_MAGIC;
}

inline std::string msg_type_str(MsgType t) {
  switch (t) {

  case MsgType::PING:
    return "PING";
  case MsgType::PONG:
    return "PONG";
  case MsgType::REGISTER_UPLOAD:
    return "REGISTER_UPLOAD";
  case MsgType::REGISTER_UPLOAD_ACK:
    return "REGISTER_UPLOAD_ACK";
  case MsgType::GET_METADATA:
    return "GET_METADATA";
  case MsgType::METADATA_RESP:
    return "METADATA_RESP";
  case MsgType::UPLOAD_CHUNK:
    return "UPLOAD_CHUNK";
  case MsgType::UPLOAD_ACK:
    return "UPLOAD_ACK";
  case MsgType::DOWNLOAD_CHUNK:
  case MsgType::DOWNLOAD_RESP:
    return "DOWNLOAD_RESP";
    return "DOWNLOAD_CHUNK";
  case MsgType::DELETE_FILE:
    return "DELETE_FILE";
  case MsgType::DELETE_ACK:
    return "DELETE_ACK";
  case MsgType::ERROR_RESP:
    return "ERROR_RESP";
  default:
    return "UNKNOWN(" + std::to_string(static_cast<int>(t)) + ")";
  }
}

inline std::string msg_type_str(uint8_t t) {
  return msg_type_str(static_cast<MsgType>(t));
}
