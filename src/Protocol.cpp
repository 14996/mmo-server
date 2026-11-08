/**
 * @file Protocol.cpp
 */
#include "Protocol.h"

#include <algorithm>
#include <cstring>

namespace proto {
namespace {

uint16_t ReadU16(const char *data) {
  return static_cast<uint16_t>((static_cast<uint8_t>(data[0]) << 8) |
                               static_cast<uint8_t>(data[1]));
}
uint32_t ReadU32(const char *data) {
  return (static_cast<uint32_t>(static_cast<uint8_t>(data[0])) << 24) |
         (static_cast<uint32_t>(static_cast<uint8_t>(data[1])) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(data[2])) << 8) |
         static_cast<uint32_t>(static_cast<uint8_t>(data[3]));
}

void WriteU16(std::string &out, uint16_t value) {
  out.push_back(static_cast<char>((value >> 8) & 0xFF));
  out.push_back(static_cast<char>(value & 0xFF));
}
void WriteU32(std::string &out, uint32_t value) {
  out.push_back(static_cast<char>((value >> 24) & 0xFF));
  out.push_back(static_cast<char>((value >> 16) & 0xFF));
  out.push_back(static_cast<char>((value >> 8) & 0xFF));
  out.push_back(static_cast<char>(value & 0xFF));
}

}  // namespace

void Writer::U8(uint8_t value) { buf_.push_back(static_cast<char>(value)); }

void Writer::U16(uint16_t value) { WriteU16(buf_, value); }

void Writer::U32(uint32_t value) { WriteU32(buf_, value); }

void Writer::U64(uint64_t value) {
  U32(static_cast<uint32_t>(value >> 32));
  U32(static_cast<uint32_t>(value & 0xFFFFFFFFULL));
}

void Writer::Str(const std::string &value) {
  size_t len = std::min<size_t>(value.size(), 0xFFFF);
  U16(static_cast<uint16_t>(len));
  buf_.append(value.data(), len);
}

bool Reader::Read(void *out, size_t len) {
  if (!ok_ || pos_ + len > size_) {
    ok_ = false;
    return false;
  }
  std::memcpy(out, data_ + pos_, len);
  pos_ += len;
  return true;
}

bool Reader::U8(uint8_t &value) { return Read(&value, 1); }

bool Reader::U16(uint16_t &value) {
  uint8_t bytes[2];
  if (!Read(bytes, 2)) {
    return false;
  }
  value = static_cast<uint16_t>((bytes[0] << 8) | bytes[1]);
  return true;
}

bool Reader::U32(uint32_t &value) {
  uint8_t bytes[4];
  if (!Read(bytes, 4)) {
    return false;
  }
  value = (static_cast<uint32_t>(bytes[0]) << 24) |
          (static_cast<uint32_t>(bytes[1]) << 16) |
          (static_cast<uint32_t>(bytes[2]) << 8) |
          static_cast<uint32_t>(bytes[3]);
  return true;
}

bool Reader::U64(uint64_t &value) {
  uint32_t high = 0;
  uint32_t low = 0;
  if (!U32(high) || !U32(low)) {
    return false;
  }
  value = (static_cast<uint64_t>(high) << 32) | low;
  return true;
}

bool Reader::Str(std::string &value, size_t max_len) {
  uint16_t len = 0;
  if (!U16(len)) {
    return false;
  }
  if (len > max_len || pos_ + len > size_) {
    ok_ = false;
    return false;
  }
  value.assign(data_ + pos_, len);
  pos_ += len;
  return true;
}

std::string EncodePacket(uint16_t msg_id, uint32_t seq, const std::string &body) {
  std::string out;
  out.reserve(kHeaderSize + body.size());
  WriteU32(out, kMagic);
  WriteU16(out, msg_id);
  WriteU16(out, 0);
  WriteU32(out, seq);
  WriteU32(out, static_cast<uint32_t>(body.size()));
  out.append(body);
  return out;
}

DecodeStatus DecodeFromBuffer(std::string &buffer, Packet &packet) {
  if (buffer.size() < kHeaderSize) {
    return DecodeStatus::kNeedMore;
  }
  const char *data = buffer.data();
  uint32_t magic = ReadU32(data);
  uint16_t msg_id = ReadU16(data + 4);
  uint32_t seq = ReadU32(data + 8);
  uint32_t body_len = ReadU32(data + 12);

  if (magic != kMagic || body_len > kMaxBodySize) {
    return DecodeStatus::kError;
  }
  if (buffer.size() < kHeaderSize + body_len) {
    return DecodeStatus::kNeedMore;
  }

  packet.msg_id = msg_id;
  packet.seq = seq;
  packet.body.assign(data + kHeaderSize, body_len);
  buffer.erase(0, kHeaderSize + body_len);
  return DecodeStatus::kOk;
}

}  // namespace proto
