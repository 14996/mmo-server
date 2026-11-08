/**
 * @file Protocol.h
 * @brief 应用层二进制协议：16 字节包头 + 大端序包体
 *
 * 包头布局：
 *   magic    u32  0x4D4D4F31 ("MMO1")
 *   msgId    u16
 *   flags    u16  预留
 *   seq      u32
 *   bodyLen  u32
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace proto {

constexpr uint32_t kMagic = 0x4D4D4F31;
constexpr size_t kHeaderSize = 16;
constexpr uint32_t kMaxBodySize = 64 * 1024;
constexpr size_t kMaxPendingBytes = 256 * 1024;

enum MsgId : uint16_t {
  kLoginReq = 1,
  kHeartbeatReq = 2,
  kMoveReq = 3,
  kChatReq = 4,
  kAttackReq = 5,
  kSkillReq = 6,
  kChangeMapReq = 7,
  kLogoutReq = 8,

  kLoginResp = 101,
  kHeartbeatResp = 102,
  kSceneInfo = 103,
  kEntityAppear = 104,
  kEntityDisappear = 105,
  kMoveNotify = 106,
  kStatsNotify = 107,
  kCombatNotify = 108,
  kDeathNotify = 109,
  kRespawnNotify = 110,
  kChatNotify = 111,
  kErrorNotify = 112,
  kNoticeNotify = 113,
  kLogoutResp = 114,
};

struct Packet {
  uint16_t msg_id{0};
  uint32_t seq{0};
  std::string body;
};

enum class DecodeStatus {
  kNeedMore,
  kOk,
  kError,
};

/// 从缓冲区中取出一个完整包；成功时会从 buffer 头部移除已消费字节
DecodeStatus DecodeFromBuffer(std::string &buffer, Packet &packet);

std::string EncodePacket(uint16_t msg_id, uint32_t seq, const std::string &body);

class Writer {
 public:
  void U8(uint8_t value);
  void U16(uint16_t value);
  void U32(uint32_t value);
  void U64(uint64_t value);
  void Str(const std::string &value);

  const std::string &Data() const { return buf_; }
  std::string Take() { return std::move(buf_); }

 private:
  std::string buf_;
};

class Reader {
 public:
  Reader(const char *data, size_t size) : data_(data), size_(size) {}

  bool U8(uint8_t &value);
  bool U16(uint16_t &value);
  bool U32(uint32_t &value);
  bool U64(uint64_t &value);
  bool Str(std::string &value, size_t max_len = 4096);

  bool ok() const { return ok_; }
  size_t remaining() const { return ok_ ? size_ - pos_ : 0; }

 private:
  bool Read(void *out, size_t len);

  const char *data_;
  size_t size_;
  size_t pos_{0};
  bool ok_{true};
};

}  // namespace proto
