/**
 * @file test_core.cpp
 * @brief 不依赖网络的核心单元测试：环形缓冲、协议编解码、限流、配置解析
 */
#include <cstdio>
#include <fstream>
#include <string>

#include "Buffer.h"
#include "Protocol.h"
#include "RateLimiter.h"
#include "ServerConfig.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

#define CHECK_EQ(a, b)                                                       \
  do {                                                                       \
    const auto lhs = (a);                                                    \
    const auto rhs = (b);                                                    \
    if (!(lhs == rhs)) {                                                     \
      std::fprintf(stderr, "FAIL %s:%d: %s != %s\n", __FILE__, __LINE__, #a, \
                   #b);                                                      \
      ++g_failures;                                                          \
    }                                                                        \
  } while (0)

void TestBuffer() {
  Buffer buffer;
  buffer.Append("hello");
  CHECK_EQ(buffer.Size(), static_cast<size_t>(5));
  CHECK_EQ(buffer.ToString(), std::string("hello"));
  CHECK_EQ(buffer.ReadableBytes(), static_cast<size_t>(5));

  buffer.Retrieve(2);
  CHECK_EQ(buffer.ToString(), std::string("llo"));
  buffer.Clear();
  CHECK_EQ(buffer.Size(), static_cast<size_t>(0));

  // 先写满再消费，使写游标回绕，验证跨环形边界的读取
  Buffer ring;
  std::string chunk(1000, 'a');
  ring.Append(chunk);
  ring.Retrieve(1000);
  std::string tail(900, 'b');
  ring.Append(tail);  // 1000 + 900 > 1024，写游标回绕
  CHECK_EQ(ring.ToString(), tail);

  // ReadPtr/ReadableBytes 需要允许调用方分段消费
  std::string joined;
  while (ring.Size() > 0) {
    const size_t piece = ring.ReadableBytes();
    CHECK(piece > 0);
    joined.append(ring.ReadPtr(), piece);
    ring.Retrieve(piece);
  }
  CHECK_EQ(joined, tail);

  // 扩容后数据顺序不变
  Buffer growing;
  growing.Append(std::string(3000, 'x'));
  growing.Retrieve(1500);
  growing.Append(std::string(2000, 'y'));
  CHECK_EQ(growing.Size(), static_cast<size_t>(3500));
  std::string expected = std::string(1500, 'x') + std::string(2000, 'y');
  CHECK_EQ(growing.ToString(), expected);
}

void TestProtocol() {
  const std::string body = "payload";
  const std::string frame = proto::EncodePacket(proto::kLoginReq, 42, body);
  CHECK_EQ(frame.size(), proto::kHeaderSize + body.size());

  std::string buffer = frame.substr(0, 10);
  proto::Packet packet;
  CHECK(proto::DecodeFromBuffer(buffer, packet) == proto::DecodeStatus::kNeedMore);

  buffer.append(frame.substr(10));
  CHECK(proto::DecodeFromBuffer(buffer, packet) == proto::DecodeStatus::kOk);
  CHECK_EQ(packet.msg_id, static_cast<uint16_t>(proto::kLoginReq));
  CHECK_EQ(packet.seq, static_cast<uint32_t>(42));
  CHECK_EQ(packet.body, body);

  // 粘包：一次读到两帧
  std::string two = frame + proto::EncodePacket(proto::kHeartbeatReq, 43, "");
  CHECK(proto::DecodeFromBuffer(two, packet) == proto::DecodeStatus::kOk);
  CHECK_EQ(packet.seq, static_cast<uint32_t>(42));
  CHECK(proto::DecodeFromBuffer(two, packet) == proto::DecodeStatus::kOk);
  CHECK_EQ(packet.seq, static_cast<uint32_t>(43));
  CHECK(two.empty());

  // 坏 magic
  std::string bad_magic = frame;
  bad_magic[0] = '\x00';
  CHECK(proto::DecodeFromBuffer(bad_magic, packet) ==
        proto::DecodeStatus::kError);

  // 协议版本不匹配
  std::string bad_version = frame;
  bad_version[6] = '\x00';
  bad_version[7] = static_cast<char>(proto::kVersion + 1);
  CHECK(proto::DecodeFromBuffer(bad_version, packet) ==
        proto::DecodeStatus::kVersionMismatch);

  // 包体长度超过上限
  std::string too_large = frame;
  const uint32_t huge = proto::kMaxBodySize + 1;
  too_large[12] = static_cast<char>((huge >> 24) & 0xFF);
  too_large[13] = static_cast<char>((huge >> 16) & 0xFF);
  too_large[14] = static_cast<char>((huge >> 8) & 0xFF);
  too_large[15] = static_cast<char>(huge & 0xFF);
  CHECK(proto::DecodeFromBuffer(too_large, packet) ==
        proto::DecodeStatus::kError);

  CHECK(proto::IsClientMsgId(proto::kLoginReq));
  CHECK(proto::IsClientMsgId(proto::kLogoutReq));
  CHECK(!proto::IsClientMsgId(proto::kLoginResp));
  CHECK(!proto::IsClientMsgId(9999));
}

void TestRateLimiter() {
  RateLimiter limiter(3, 1000);
  CHECK(limiter.Allow(10));
  CHECK(limiter.Allow(20));
  CHECK(limiter.Allow(30));
  CHECK(!limiter.Allow(40));
  CHECK_EQ(limiter.Count(), static_cast<uint32_t>(3));

  // 窗口滚动后重新计数
  CHECK(limiter.Allow(1010));
  CHECK(limiter.Allow(1020));
  CHECK(limiter.Allow(1030));
  CHECK(!limiter.Allow(1040));
}

void TestConfig() {
  const char *path = "test_server_config_tmp.conf";
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "# comment\r\n";
    out << "server.ip=127.0.0.1\r\n";
    out << "server.port = 9100\r\n";
    out << "net.heartbeat_timeout_ms=5000\n";
    out << "net.max_packets_per_sec=120\n";
    out << "game.save_interval_ms=2000\n";
    out << "game.offline_retain_ms=0\n";
    out << "game.aoi_cell_size=4\n";
  }

  ServerConfig config;
  std::string error;
  CHECK(LoadServerConfig(path, config, error, false));
  CHECK_EQ(config.ip, std::string("127.0.0.1"));
  CHECK_EQ(config.port, static_cast<uint16_t>(9100));
  CHECK_EQ(config.heartbeat_timeout_ms, static_cast<int64_t>(5000));
  CHECK_EQ(config.max_packets_per_sec, static_cast<uint32_t>(120));
  CHECK_EQ(config.save_interval_ms, static_cast<int64_t>(2000));
  CHECK_EQ(config.offline_retain_ms, static_cast<int64_t>(0));
  CHECK_EQ(config.aoi_cell_size, 4);

  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "server.port=70000\n";
  }
  ServerConfig invalid;
  error.clear();
  CHECK(!LoadServerConfig(path, invalid, error, false));
  CHECK(!error.empty());

  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "server.unknown_key=1\n";
  }
  error.clear();
  CHECK(!LoadServerConfig(path, invalid, error, false));
  CHECK(!error.empty());

  std::remove(path);

  ServerConfig fallback;
  error.clear();
  CHECK(LoadServerConfig("__missing_config__.conf", fallback, error, true));
  CHECK(!LoadServerConfig("__missing_config__.conf", fallback, error, false));
  CHECK(!error.empty());
}

}  // namespace

int main() {
  TestBuffer();
  TestProtocol();
  TestRateLimiter();
  TestConfig();

  if (g_failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("all core tests passed\n");
  return 0;
}
