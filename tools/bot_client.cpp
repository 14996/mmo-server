/**
 * @file bot_client.cpp
 * @brief 无界面压测/联调机器人：
 *        批量登录 -> 接收场景与 AOI 广播 -> 寻怪 -> 移动/普攻/技能 -> 心跳/聊天
 *
 * 用途：
 *   - 端到端验证服务端协议、AOI、战斗、死亡复活、断线清理；
 *   - 作为简单压测工具观察在线数、包量、击杀数等指标。
 * 注意：它只是测试工具，不是游戏客户端。
 */
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Messages.h"
#include "Platform.h"
#include "Protocol.h"
#include "util.h"

#ifndef _WIN32
#include <netdb.h>
#include <sys/select.h>
#endif

namespace {

constexpr uint8_t kEntityMonster = 2;
constexpr const char *kPassword = "bot-password";
constexpr int kPortal1X = 16;
constexpr int kPortal1Y = 10;

volatile std::sig_atomic_t g_signal = 0;
std::atomic<bool> g_stop{false};
std::mutex g_log_mutex;

void HandleSignal(int /*sig*/) { g_signal = 1; }

void BotLog(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  std::lock_guard<std::mutex> lock(g_log_mutex);
  std::fprintf(stderr, "[bot] ");
  std::vfprintf(stderr, fmt, args);
  std::fprintf(stderr, "\n");
  std::fflush(stderr);
  va_end(args);
}

struct Options {
  std::string host{"127.0.0.1"};
  uint16_t port{9000};
  int bots{10};
  int duration_s{60};
  std::string prefix{"bot"};
  int start_index{0};
};

struct Aggregate {
  std::atomic<uint64_t> kills{0};
  std::atomic<uint64_t> deaths{0};
  std::atomic<uint64_t> errors{0};
  std::atomic<uint64_t> packets{0};
  std::atomic<uint64_t> bytes{0};
  std::atomic<uint64_t> chats{0};
  std::atomic<uint64_t> notices{0};
  std::atomic<uint64_t> damage_done{0};
  std::atomic<uint64_t> damage_taken{0};
  std::atomic<uint64_t> map_changes{0};
  std::atomic<uint64_t> drops{0};
  std::atomic<uint64_t> connect_fail{0};
  std::atomic<int> online{0};
  std::atomic<int> max_level{1};
};

int Chebyshev(int x1, int y1, int x2, int y2) {
  int dx = x1 > x2 ? x1 - x2 : x2 - x1;
  int dy = y1 > y2 ? y1 - y2 : y2 - y1;
  return dx > dy ? dx : dy;
}

class Bot {
 public:
  Bot(const Options &options, int index, Aggregate &agg)
      : options_(options),
        index_(index),
        agg_(agg),
        rng_(static_cast<uint32_t>(NowMs()) ^
             (static_cast<uint32_t>(index) * 2654435761U)) {
    name_ = options_.prefix + std::to_string(options_.start_index + index);
    PickPatrolTarget();
  }

  ~Bot() {
    if (fd_ != kInvalidSocket) {
      CloseSocketHandle(fd_);
      fd_ = kInvalidSocket;
    }
  }

  void Run(int64_t end_ms) {
    if (!Connect()) {
      agg_.connect_fail.fetch_add(1, std::memory_order_relaxed);
      BotLog("%s connect failed", name_.c_str());
      return;
    }
    if (!Login()) {
      agg_.connect_fail.fetch_add(1, std::memory_order_relaxed);
      BotLog("%s login failed", name_.c_str());
      return;
    }

    agg_.online.fetch_add(1, std::memory_order_relaxed);
    while (!g_stop.load(std::memory_order_relaxed) && NowMs() < end_ms) {
      if (!ReadOne(50)) {
        agg_.drops.fetch_add(1, std::memory_order_relaxed);
        BotLog("%s connection closed by server", name_.c_str());
        break;
      }
      Tick(NowMs());
    }

    if (logged_in_) {
      if (index_ % 2 == 0) {
        // 偶数号正常登出，奇数号直接断开，覆盖两条清理路径
        SendPacket(proto::kLogoutReq, "");
        ReadOne(150);
      }
      agg_.online.fetch_sub(1, std::memory_order_relaxed);
    }
    if (fd_ != kInvalidSocket) {
      CloseSocketHandle(fd_);
      fd_ = kInvalidSocket;
    }
  }

 private:
  bool Connect() {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_text[16];
    std::snprintf(port_text, sizeof(port_text), "%u",
                  static_cast<unsigned>(options_.port));

    addrinfo *result = nullptr;
    if (getaddrinfo(options_.host.c_str(), port_text, &hints, &result) != 0) {
      return false;
    }

    SocketHandle fd = kInvalidSocket;
    for (addrinfo *it = result; it != nullptr; it = it->ai_next) {
      fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
      if (fd == kInvalidSocket) {
        continue;
      }
      if (::connect(fd, it->ai_addr, static_cast<SockLen>(it->ai_addrlen)) == 0) {
        break;
      }
      CloseSocketHandle(fd);
      fd = kInvalidSocket;
    }
    freeaddrinfo(result);
    fd_ = fd;
    return fd_ != kInvalidSocket;
  }

  bool Login() {
    proto::LoginReq request;
    request.name = name_;
    request.password = kPassword;
    if (!SendPacket(proto::kLoginReq, proto::EncodeLoginReq(request))) {
      return false;
    }

    int64_t deadline = NowMs() + 5000;
    while (NowMs() < deadline) {
      if (!ReadOne(100)) {
        return false;
      }
      if (login_failed_) {
        return false;
      }
      if (logged_in_ && in_scene_) {
        return true;
      }
    }
    BotLog("%s login timeout", name_.c_str());
    return false;
  }

  bool SendPacket(uint16_t msg_id, const std::string &body) {
    if (fd_ == kInvalidSocket) {
      return false;
    }
    std::string packet = proto::EncodePacket(msg_id, ++seq_, body);
    size_t sent = 0;
    while (sent < packet.size()) {
      ssize_t n = SocketWrite(fd_, packet.data() + sent,
                              static_cast<int>(packet.size() - sent));
      if (n > 0) {
        sent += static_cast<size_t>(n);
        continue;
      }
      int err = LastSocketError();
      if (IsInterrupted(err)) {
        continue;
      }
      if (IsWouldBlock(err)) {
        SleepMs(1);
        continue;
      }
      return false;
    }
    return true;
  }

  /// 等待最多 timeout_ms，读取并处理所有完整包；连接断开返回 false
  bool ReadOne(int timeout_ms) {
    if (fd_ == kInvalidSocket) {
      return false;
    }

    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(fd_, &read_set);
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    int ready = ::select(static_cast<int>(fd_) + 1, &read_set, nullptr, nullptr,
                         &timeout);
    if (ready < 0) {
      int err = LastSocketError();
      return IsInterrupted(err) || IsWouldBlock(err);
    }
    if (ready == 0) {
      return true;
    }

    char buffer[8192];
    ssize_t n = SocketRead(fd_, buffer, static_cast<int>(sizeof(buffer)));
    if (n > 0) {
      in_buffer_.append(buffer, static_cast<size_t>(n));
      agg_.bytes.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
    } else if (n == 0) {
      return false;
    } else {
      int err = LastSocketError();
      if (IsInterrupted(err) || IsWouldBlock(err)) {
        return true;
      }
      return false;
    }

    proto::Packet packet;
    while (true) {
      proto::DecodeStatus status =
          proto::DecodeFromBuffer(in_buffer_, packet);
      if (status == proto::DecodeStatus::kNeedMore) {
        break;
      }
      if (status == proto::DecodeStatus::kError) {
        return false;
      }
      agg_.packets.fetch_add(1, std::memory_order_relaxed);
      HandlePacket(packet);
    }
    return true;
  }

  void HandlePacket(const proto::Packet &packet) {
    switch (packet.msg_id) {
      case proto::kLoginResp: {
        proto::LoginResp resp;
        if (!proto::DecodeLoginResp(packet.body, resp)) {
          login_failed_ = true;
          return;
        }
        if (resp.ok == 0) {
          login_failed_ = true;
          BotLog("%s login rejected: %s", name_.c_str(), resp.error.c_str());
          return;
        }
        player_id_ = resp.player_id;
        logged_in_ = true;
        break;
      }
      case proto::kSceneInfo: {
        proto::SceneInfo info;
        if (!proto::DecodeSceneInfo(packet.body, info)) {
          return;
        }
        if (in_scene_ && map_id_ != static_cast<int>(info.map_id)) {
          agg_.map_changes.fetch_add(1, std::memory_order_relaxed);
        }
        map_id_ = static_cast<int>(info.map_id);
        x_ = info.x;
        y_ = info.y;
        level_ = info.level;
        hp_ = static_cast<int>(info.hp);
        max_hp_ = static_cast<int>(info.max_hp);
        mp_ = static_cast<int>(info.mp);
        max_mp_ = static_cast<int>(info.max_mp);
        atk_ = static_cast<int>(info.atk);
        entities_.clear();
        for (const auto &entity : info.entities) {
          entities_[entity.id] = entity;
        }
        dead_ = false;
        in_scene_ = true;
        scene_entered_ms_ = NowMs();
        UpdateMaxLevel(info.level);
        break;
      }
      case proto::kStatsNotify: {
        proto::StatsInfo info;
        if (!proto::DecodeStatsInfo(packet.body, info)) {
          return;
        }
        level_ = info.level;
        hp_ = static_cast<int>(info.hp);
        max_hp_ = static_cast<int>(info.max_hp);
        mp_ = static_cast<int>(info.mp);
        max_mp_ = static_cast<int>(info.max_mp);
        atk_ = static_cast<int>(info.atk);
        UpdateMaxLevel(info.level);
        if (hp_ > 0) {
          dead_ = false;
        }
        break;
      }
      case proto::kEntityAppear: {
        proto::EntityInfo entity;
        if (!proto::DecodeEntityAppear(packet.body, entity)) {
          return;
        }
        entities_[entity.id] = entity;
        break;
      }
      case proto::kEntityDisappear: {
        uint64_t entity_id = 0;
        if (proto::DecodeEntityDisappear(packet.body, entity_id)) {
          entities_.erase(entity_id);
        }
        break;
      }
      case proto::kMoveNotify: {
        proto::MoveNotify move;
        if (!proto::DecodeMoveNotify(packet.body, move)) {
          return;
        }
        if (move.id == player_id_) {
          x_ = move.x;
          y_ = move.y;
          return;
        }
        auto it = entities_.find(move.id);
        if (it != entities_.end()) {
          it->second.x = move.x;
          it->second.y = move.y;
        }
        break;
      }
      case proto::kCombatNotify: {
        proto::CombatNotify combat;
        if (!proto::DecodeCombatNotify(packet.body, combat)) {
          return;
        }
        if (combat.attacker_id == player_id_) {
          agg_.damage_done.fetch_add(combat.damage,
                                     std::memory_order_relaxed);
        }
        if (combat.target_id == player_id_) {
          agg_.damage_taken.fetch_add(combat.damage,
                                      std::memory_order_relaxed);
          hp_ = static_cast<int>(combat.target_hp);
        }
        auto it = entities_.find(combat.target_id);
        if (it != entities_.end()) {
          it->second.hp = combat.target_hp;
          it->second.alive = combat.target_alive;
        }
        break;
      }
      case proto::kDeathNotify: {
        proto::DeathNotify death;
        if (!proto::DecodeDeathNotify(packet.body, death)) {
          return;
        }
        auto dead_it = entities_.find(death.entity_id);
        if (dead_it != entities_.end()) {
          dead_it->second.alive = 0;
        }
        if (death.entity_id == player_id_) {
          dead_ = true;
          hp_ = 0;
          agg_.deaths.fetch_add(1, std::memory_order_relaxed);
        } else if (death.entity_type == kEntityMonster &&
                   death.killer_id == player_id_) {
          agg_.kills.fetch_add(1, std::memory_order_relaxed);
        }
        break;
      }
      case proto::kRespawnNotify: {
        proto::RespawnNotify respawn;
        if (!proto::DecodeRespawnNotify(packet.body, respawn)) {
          return;
        }
        if (respawn.entity_id == player_id_) {
          dead_ = false;
          x_ = respawn.x;
          y_ = respawn.y;
          hp_ = static_cast<int>(respawn.hp);
          max_hp_ = static_cast<int>(respawn.max_hp);
        }
        break;
      }
      case proto::kChatNotify:
        agg_.chats.fetch_add(1, std::memory_order_relaxed);
        break;
      case proto::kNoticeNotify:
        agg_.notices.fetch_add(1, std::memory_order_relaxed);
        break;
      case proto::kErrorNotify: {
        proto::ErrorNotify error;
        if (!proto::DecodeErrorNotify(packet.body, error)) {
          return;
        }
        agg_.errors.fetch_add(1, std::memory_order_relaxed);
        if (logged_errors_ < 3) {
          ++logged_errors_;
          BotLog("%s server error %u: %s", name_.c_str(), error.code,
                 error.message.c_str());
        }
        break;
      }
      default:
        break;
    }
  }

  void Tick(int64_t now_ms) {
    if (!logged_in_ || !in_scene_ || dead_) {
      return;
    }

    if (now_ms - last_heartbeat_ms_ >= 3000) {
      last_heartbeat_ms_ = now_ms;
      SendPacket(proto::kHeartbeatReq, "");
    }

    if (now_ms - last_chat_ms_ >= 15000 + static_cast<int64_t>(rng_() % 10000)) {
      last_chat_ms_ = now_ms;
      proto::ChatReq chat;
      chat.text = name_ + " 正在打怪";
      SendPacket(proto::kChatReq, proto::EncodeChatReq(chat));
    }

    // 部分机器人 15 秒后去传送门，覆盖跨地图/AOI 重建
    if (map_id_ == 1 && index_ % 4 == 0 &&
        now_ms - scene_entered_ms_ >= 15000) {
      if (Chebyshev(x_, y_, kPortal1X, kPortal1Y) <= 1) {
        if (now_ms - last_change_map_ms_ >= 1000) {
          last_change_map_ms_ = now_ms;
          proto::ChangeMapReq change;
          change.map_id = 2;
          SendPacket(proto::kChangeMapReq, proto::EncodeChangeMapReq(change));
        }
      } else if (now_ms - last_move_ms_ >= 170) {
        last_move_ms_ = now_ms;
        MoveToward(kPortal1X, kPortal1Y);
      }
      return;
    }

    const proto::EntityInfo *target = nullptr;
    int best_distance = 1 << 30;
    for (const auto &item : entities_) {
      const proto::EntityInfo &entity = item.second;
      if (entity.type != kEntityMonster || entity.alive == 0) {
        continue;
      }
      int distance = Chebyshev(x_, y_, entity.x, entity.y);
      if (distance < best_distance) {
        best_distance = distance;
        target = &entity;
      }
    }

    if (target == nullptr) {
      // 没看到怪：朝随机巡逻点走，尽快进入刷怪区
      if (now_ms - last_move_ms_ >= 170) {
        last_move_ms_ = now_ms;
        MoveToward(patrol_x_, patrol_y_);
        if (x_ == patrol_x_ && y_ == patrol_y_) {
          PickPatrolTarget();
        }
      }
      return;
    }

    if (best_distance <= 1) {
      if (now_ms - last_attack_ms_ >= 850) {
        last_attack_ms_ = now_ms;
        proto::AttackReq attack;
        attack.target_id = target->id;
        SendPacket(proto::kAttackReq, proto::EncodeAttackReq(attack));
      }
      return;
    }

    if (best_distance <= 6 && mp_ >= 15 && now_ms - last_skill_ms_ >= 4000) {
      last_skill_ms_ = now_ms;
      proto::SkillReq skill;
      skill.skill_id = 1002;
      skill.target_id = target->id;
      SendPacket(proto::kSkillReq, proto::EncodeSkillReq(skill));
      return;
    }

    if (now_ms - last_move_ms_ >= 170) {
      last_move_ms_ = now_ms;
      MoveToward(target->x, target->y);
    }
  }

  void MoveToward(int target_x, int target_y) {
    uint8_t dir;
    int dx = target_x - x_;
    int dy = target_y - y_;
    if (dx == 0 && dy == 0) {
      return;
    }
    if (std::abs(dx) >= std::abs(dy)) {
      dir = dx > 0 ? 3 : 2;
    } else {
      dir = dy > 0 ? 1 : 0;
    }
    proto::MoveReq move;
    move.dir = dir;
    SendPacket(proto::kMoveReq, proto::EncodeMoveReq(move));
  }

  void PickPatrolTarget() {
    patrol_x_ = 30 + static_cast<int>(rng_() % 40);
    patrol_y_ = 10 + static_cast<int>(rng_() % 30);
  }

  void UpdateMaxLevel(uint8_t level) {
    int current = agg_.max_level.load(std::memory_order_relaxed);
    while (level > current &&
           !agg_.max_level.compare_exchange_weak(current, level)) {
    }
  }

  Options options_;
  int index_;
  Aggregate &agg_;
  std::mt19937 rng_;

  SocketHandle fd_{kInvalidSocket};
  std::string in_buffer_;
  std::string name_;
  uint64_t player_id_{0};
  uint32_t seq_{0};
  bool logged_in_{false};
  bool login_failed_{false};
  bool in_scene_{false};
  bool dead_{false};
  int logged_errors_{0};

  int map_id_{1};
  int x_{0};
  int y_{0};
  int patrol_x_{0};
  int patrol_y_{0};
  uint8_t level_{1};
  int hp_{0};
  int max_hp_{0};
  int mp_{0};
  int max_mp_{0};
  int atk_{0};

  int64_t last_move_ms_{0};
  int64_t last_attack_ms_{0};
  int64_t last_skill_ms_{0};
  int64_t last_chat_ms_{0};
  int64_t last_heartbeat_ms_{0};
  int64_t scene_entered_ms_{0};
  int64_t last_change_map_ms_{0};

  std::unordered_map<uint64_t, proto::EntityInfo> entities_;
};

void PrintOptions(const Options &options) {
  std::printf(
      "bot client -> %s:%u, bots=%d, duration=%ds, names=%s%d..%s%d\n",
      options.host.c_str(), static_cast<unsigned>(options.port), options.bots,
      options.duration_s, options.prefix.c_str(), options.start_index,
      options.prefix.c_str(), options.start_index + options.bots - 1);
}

}  // namespace

int main(int argc, char **argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--host" && i + 1 < argc) {
      options.host = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      options.port = static_cast<uint16_t>(std::atoi(argv[++i]));
    } else if (arg == "--bots" && i + 1 < argc) {
      options.bots = std::atoi(argv[++i]);
    } else if (arg == "--duration" && i + 1 < argc) {
      options.duration_s = std::atoi(argv[++i]);
    } else if (arg == "--prefix" && i + 1 < argc) {
      options.prefix = argv[++i];
    } else if (arg == "--start-index" && i + 1 < argc) {
      options.start_index = std::atoi(argv[++i]);
    } else if (arg == "--help" || arg == "-h") {
      std::printf(
          "Usage: %s [--host 127.0.0.1] [--port 9000] [--bots 10] "
          "[--duration 60] [--prefix bot] [--start-index 0]\n",
          argv[0]);
      return 0;
    } else {
      std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
      return 1;
    }
  }

  if (options.bots <= 0 || options.duration_s <= 0) {
    std::fprintf(stderr, "bots and duration must be positive\n");
    return 1;
  }

  IgnoreSigPipe();
  if (!SocketInit()) {
    std::fprintf(stderr, "socket init failed: %d\n", LastSocketError());
    return 1;
  }
  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif

  PrintOptions(options);

  Aggregate agg;
  std::vector<std::unique_ptr<Bot>> bots;
  std::vector<std::thread> threads;
  bots.reserve(static_cast<size_t>(options.bots));
  threads.reserve(static_cast<size_t>(options.bots));

  const int64_t end_ms = NowMs() + static_cast<int64_t>(options.duration_s) * 1000;
  for (int i = 0; i < options.bots; ++i) {
    bots.push_back(std::make_unique<Bot>(options, i, agg));
    threads.emplace_back([&bots, i, end_ms]() { bots[i]->Run(end_ms); });
  }

  int64_t start_ms = NowMs();
  uint64_t last_packets = 0;
  while (NowMs() < end_ms && g_signal == 0) {
    SleepMs(1000);
    uint64_t packets = agg.packets.load(std::memory_order_relaxed);
    double seconds =
        static_cast<double>(NowMs() - start_ms) / 1000.0;
    std::printf(
        "[%5.1fs] online=%d kills=%llu deaths=%llu errors=%llu drops=%llu "
        "packets=%llu (%.0f/s) bytes=%llu maxLv=%d dmg=%llu/%llu\n",
        seconds, agg.online.load(std::memory_order_relaxed),
        static_cast<unsigned long long>(agg.kills.load()),
        static_cast<unsigned long long>(agg.deaths.load()),
        static_cast<unsigned long long>(agg.errors.load()),
        static_cast<unsigned long long>(agg.drops.load()),
        static_cast<unsigned long long>(packets),
        seconds > 0 ? static_cast<double>(packets - last_packets) / 1.0 : 0.0,
        static_cast<unsigned long long>(agg.bytes.load()),
        agg.max_level.load(std::memory_order_relaxed),
        static_cast<unsigned long long>(agg.damage_done.load()),
        static_cast<unsigned long long>(agg.damage_taken.load()));
    std::fflush(stdout);
    last_packets = packets;
  }

  g_stop.store(true, std::memory_order_relaxed);
  for (auto &thread : threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }

  std::printf(
      "\n=== final ===\n"
      "kills=%llu deaths=%llu errors=%llu drops=%llu connect_fail=%llu\n"
      "packets=%llu bytes=%llu chats=%llu notices=%llu max_level=%d "
      "map_changes=%llu\n"
      "damage_done=%llu damage_taken=%llu\n",
      static_cast<unsigned long long>(agg.kills.load()),
      static_cast<unsigned long long>(agg.deaths.load()),
      static_cast<unsigned long long>(agg.errors.load()),
      static_cast<unsigned long long>(agg.drops.load()),
      static_cast<unsigned long long>(agg.connect_fail.load()),
      static_cast<unsigned long long>(agg.packets.load()),
      static_cast<unsigned long long>(agg.bytes.load()),
      static_cast<unsigned long long>(agg.chats.load()),
      static_cast<unsigned long long>(agg.notices.load()),
      agg.max_level.load(std::memory_order_relaxed),
      static_cast<unsigned long long>(agg.map_changes.load()),
      static_cast<unsigned long long>(agg.damage_done.load()),
      static_cast<unsigned long long>(agg.damage_taken.load()));
  std::fflush(stdout);

  SocketCleanup();
  return 0;
}
