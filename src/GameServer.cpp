/**
 * @file GameServer.cpp
 */
#include "GameServer.h"

#include <utility>
#include <vector>

#include "Buffer.h"
#include "Connection.h"
#include "EventLoop.h"
#include "Server.h"
#include "util.h"

GameServer::GameServer(Server *server, const ServerConfig &config)
    : server_(server),
      config_(config),
      world_([this](uint64_t player_id, uint16_t msg_id,
                    const std::string &body) {
              SendToPlayerLocked(player_id, msg_id, body);
            },
            WorldRuntimeConfig{config.aoi_cell_size, config.save_interval_ms,
                               config.offline_retain_ms}) {
  ASSERT(server_ != nullptr, "server must not be null");
  if (config_.heartbeat_timeout_ms <= 0) {
    config_.heartbeat_timeout_ms = 30000;
  }
  if (config_.max_packets_per_sec == 0) {
    config_.max_packets_per_sec = 300;
  }
}

GameServer::~GameServer() { Stop(); }

void GameServer::Start() {
  if (running_.exchange(true)) {
    return;
  }
  const int64_t now = NowMs();
  last_idle_scan_ms_ = now;
  last_metrics_ms_ = now;
  last_metrics_packets_ = total_packets_.load(std::memory_order_relaxed);
  last_metrics_bytes_ = total_bytes_.load(std::memory_order_relaxed);

  server_->SetOnNewConnection(
      [this](Connection *conn) { OnNewConnection(conn); });
  server_->SetMessageCallback([this](Connection *conn) { OnMessage(conn); });
  tick_thread_ = std::thread([this]() { TickLoop(); });
  LogInfo("game server started (tick=50ms heartbeat_timeout=%lldms "
          "rate_limit=%u/s aoi_cell=%d)",
          static_cast<long long>(config_.heartbeat_timeout_ms),
          config_.max_packets_per_sec, config_.aoi_cell_size);
}

void GameServer::Stop() {
  bool was_running = running_.exchange(false);
  if (tick_thread_.joinable()) {
    tick_thread_.join();
  }
  if (was_running) {
    std::lock_guard<std::mutex> lock(mutex_);
    world_.SaveAll();
    LogInfo("game server stopped, world saved");
  }
}

void GameServer::TickLoop() {
  while (running_.load()) {
    const int64_t now = NowMs();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      world_.Tick(now);

      if (now - last_idle_scan_ms_ >= 1000) {
        last_idle_scan_ms_ = now;
        TickIdleSessionsLocked(now);
      }
      LogMetricsLocked(now);
    }
    SleepMs(50);
  }
}

void GameServer::TickIdleSessionsLocked(int64_t now_ms) {
  std::vector<Connection *> timed_out;
  for (auto &item : sessions_) {
    Session &session = item.second;
    if (session.close_requested) {
      continue;
    }
    if (now_ms - session.last_active_ms < config_.heartbeat_timeout_ms) {
      continue;
    }
    session.close_requested = true;
    timed_out.push_back(item.first);
  }
  if (timed_out.empty()) {
    return;
  }

  // 踢线前先把在线角色标记为离线并落盘，避免超时玩家丢进度
  bool need_save = false;
  for (Connection *conn : timed_out) {
    auto it = sessions_.find(conn);
    if (it == sessions_.end()) {
      continue;
    }
    Session &session = it->second;
    if (session.state == SessionState::kOnline && session.player_id != 0) {
      world_.OnDisconnect(session.player_id);
      need_save = true;
    }
  }
  if (need_save) {
    world_.SaveAll();
  }

  for (Connection *conn : timed_out) {
    auto it = sessions_.find(conn);
    if (it == sessions_.end()) {
      continue;
    }
    const uint64_t player_id = it->second.player_id;
    ++idle_kicks_;
    LogWarn("heartbeat timeout, closing connection (player=%llu idle=%lldms)",
            static_cast<unsigned long long>(player_id),
            static_cast<long long>(now_ms - it->second.last_active_ms));

    // 关键：仍在持锁状态下投递关闭任务。关闭回调需要拿同一把锁，
    // 因此它只能在本次投递之后才可能把 delete 任务排到同一个 loop，
    // FIFO 保证 close 一定先于 delete 执行，避免 tick 线程持有悬空指针。
    EventLoop *loop = conn->GetLoop();
    if (loop != nullptr) {
      loop->AddTask([conn]() { conn->Close(); });
    }
  }
}

void GameServer::LogMetricsLocked(int64_t now_ms) {
  if (now_ms - last_metrics_ms_ < 10000) {
    return;
  }
  int online = 0;
  for (const auto &item : sessions_) {
    if (item.second.state == SessionState::kOnline) {
      ++online;
    }
  }

  const uint64_t packets = total_packets_.load(std::memory_order_relaxed);
  const uint64_t bytes = total_bytes_.load(std::memory_order_relaxed);
  const int64_t elapsed = now_ms - last_metrics_ms_;
  const double seconds = static_cast<double>(elapsed) / 1000.0;
  const double qps = seconds > 0.0
                         ? static_cast<double>(packets - last_metrics_packets_) /
                               seconds
                         : 0.0;
  const double bps = seconds > 0.0
                         ? static_cast<double>(bytes - last_metrics_bytes_) /
                               seconds
                         : 0.0;

  LogInfo("metrics: online=%d sessions=%zu qps=%.1f inbound=%.0fB/s "
          "total_conns=%llu idle_kicks=%llu rate_limit_kicks=%llu "
          "invalid_packets=%llu",
          online, sessions_.size(), qps, bps,
          static_cast<unsigned long long>(
              total_connections_.load(std::memory_order_relaxed)),
          static_cast<unsigned long long>(
              idle_kicks_.load(std::memory_order_relaxed)),
          static_cast<unsigned long long>(
              rate_limit_kicks_.load(std::memory_order_relaxed)),
          static_cast<unsigned long long>(
              invalid_packets_.load(std::memory_order_relaxed)));

  last_metrics_ms_ = now_ms;
  last_metrics_packets_ = packets;
  last_metrics_bytes_ = bytes;
}

void GameServer::OnNewConnection(Connection *conn) {
  conn->SetMessageCallback([this](Connection *c) { OnMessage(c); });
  conn->SetCloseCallback([this](Connection *c) { OnConnectionClose(c); });

  std::lock_guard<std::mutex> lock(mutex_);
  Session session;
  session.last_active_ms = NowMs();
  session.rate_limiter = RateLimiter(config_.max_packets_per_sec, 1000);
  sessions_[conn] = std::move(session);
  ++total_connections_;
}

void GameServer::OnConnectionClose(Connection *conn) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(conn);
  if (it == sessions_.end()) {
    return;
  }

  if (it->second.state == SessionState::kOnline && it->second.player_id != 0) {
    uint64_t player_id = it->second.player_id;
    world_.OnDisconnect(player_id);
    auto conn_it = player_conns_.find(player_id);
    if (conn_it != player_conns_.end() && conn_it->second == conn) {
      player_conns_.erase(conn_it);
    }
  }
  sessions_.erase(it);
}

void GameServer::OnMessage(Connection *conn) {
  Session *session = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(conn);
    if (it == sessions_.end() || it->second.close_requested) {
      return;
    }
    session = &it->second;
  }

  Buffer *read_buffer = conn->GetReadBuffer();
  const size_t n = read_buffer->Size();
  if (n > 0) {
    read_buffer->AppendTo(session->parse_buffer);
    read_buffer->Clear();
    total_bytes_.fetch_add(n, std::memory_order_relaxed);
  }

  if (session->parse_buffer.size() > proto::kMaxPendingBytes) {
    ++invalid_packets_;
    LogWarn("connection frame buffer overflow, closing");
    conn->Close();
    return;
  }

  proto::Packet packet;
  while (true) {
    proto::DecodeStatus status =
        proto::DecodeFromBuffer(session->parse_buffer, packet);
    if (status == proto::DecodeStatus::kNeedMore) {
      break;
    }
    if (status == proto::DecodeStatus::kVersionMismatch) {
      ++invalid_packets_;
      LogWarn("protocol version mismatch (server=%u), closing connection",
              static_cast<unsigned>(proto::kVersion));
      conn->Close();
      return;
    }
    if (status == proto::DecodeStatus::kError) {
      ++invalid_packets_;
      LogWarn("invalid frame header, closing connection");
      conn->Close();
      return;
    }

    // 每连接固定窗口限流：超限直接断连，避免单连接刷包拖垮整个 reactor
    bool allowed = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = sessions_.find(conn);
      if (it == sessions_.end() || it->second.close_requested) {
        return;
      }
      session = &it->second;
      const int64_t now = NowMs();
      allowed = session->rate_limiter.Allow(now);
      if (allowed) {
        session->last_active_ms = now;
      }
    }
    if (!allowed) {
      ++rate_limit_kicks_;
      LogWarn("rate limit exceeded (%u/s), closing connection",
              config_.max_packets_per_sec);
      conn->Close();
      return;
    }

    if (!proto::IsClientMsgId(packet.msg_id)) {
      ++invalid_packets_;
      LogWarn("drop illegal client msg_id=%u (player=%llu)",
              static_cast<unsigned>(packet.msg_id),
              static_cast<unsigned long long>(session->player_id));
      continue;
    }

    ++total_packets_;
    Dispatch(conn, *session, packet);
  }
}

void GameServer::Dispatch(Connection *conn, Session &session,
                          const proto::Packet &packet) {
  switch (packet.msg_id) {
    case proto::kLoginReq:
      HandleLogin(conn, session, packet);
      return;
    case proto::kHeartbeatReq:
      QueueSend(conn, proto::kHeartbeatResp, packet.seq, "");
      return;
    case proto::kLogoutReq:
      HandleLogout(conn, session, packet);
      return;
    default:
      break;
  }

  if (session.state != SessionState::kOnline) {
    SendErrorTo(conn, packet.seq, 401,
                session.state == SessionState::kAuthenticating ? "正在登录"
                                                               : "请先登录");
    return;
  }
  const uint64_t player_id = session.player_id;

  switch (packet.msg_id) {
    case proto::kMoveReq: {
      proto::MoveReq req;
      if (!proto::DecodeMoveReq(packet.body, req)) {
        SendErrorTo(conn, packet.seq, 400, "MoveReq 格式错误");
        return;
      }
      std::lock_guard<std::mutex> lock(mutex_);
      world_.HandleMove(player_id, req.dir);
      break;
    }
    case proto::kChatReq: {
      proto::ChatReq req;
      if (!proto::DecodeChatReq(packet.body, req)) {
        SendErrorTo(conn, packet.seq, 400, "ChatReq 格式错误");
        return;
      }
      std::lock_guard<std::mutex> lock(mutex_);
      world_.HandleChat(player_id, req.text);
      break;
    }
    case proto::kAttackReq: {
      proto::AttackReq req;
      if (!proto::DecodeAttackReq(packet.body, req)) {
        SendErrorTo(conn, packet.seq, 400, "AttackReq 格式错误");
        return;
      }
      std::lock_guard<std::mutex> lock(mutex_);
      world_.HandleAttack(player_id, req.target_id);
      break;
    }
    case proto::kSkillReq: {
      proto::SkillReq req;
      if (!proto::DecodeSkillReq(packet.body, req)) {
        SendErrorTo(conn, packet.seq, 400, "SkillReq 格式错误");
        return;
      }
      std::lock_guard<std::mutex> lock(mutex_);
      world_.HandleSkill(player_id, req.skill_id, req.target_id);
      break;
    }
    case proto::kChangeMapReq: {
      proto::ChangeMapReq req;
      if (!proto::DecodeChangeMapReq(packet.body, req)) {
        SendErrorTo(conn, packet.seq, 400, "ChangeMapReq 格式错误");
        return;
      }
      std::lock_guard<std::mutex> lock(mutex_);
      world_.HandleChangeMap(player_id, req.map_id);
      break;
    }
    default:
      SendErrorTo(conn, packet.seq, 400, "未知消息类型");
      break;
  }
}

void GameServer::HandleLogin(Connection *conn, Session &session,
                             const proto::Packet &packet) {
  if (session.state != SessionState::kUnauthenticated) {
    proto::LoginResp resp;
    resp.ok = 0;
    resp.error = session.state == SessionState::kOnline ? "当前连接已经登录"
                                                        : "登录处理中";
    QueueSend(conn, proto::kLoginResp, packet.seq, proto::EncodeLoginResp(resp));
    return;
  }

  proto::LoginReq req;
  if (!proto::DecodeLoginReq(packet.body, req)) {
    proto::LoginResp resp;
    resp.ok = 0;
    resp.error = "LoginReq 格式错误";
    QueueSend(conn, proto::kLoginResp, packet.seq, proto::EncodeLoginResp(resp));
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (session.state != SessionState::kUnauthenticated) {
    return;
  }
  session.state = SessionState::kAuthenticating;

  std::string error;
  bool is_new = false;
  uint64_t player_id = world_.Login(req.name, req.password, error, is_new);
  if (player_id == 0) {
    session.state = SessionState::kUnauthenticated;
    proto::LoginResp resp;
    resp.ok = 0;
    resp.error = error;
    QueueSend(conn, proto::kLoginResp, packet.seq, proto::EncodeLoginResp(resp));
    return;
  }

  session.state = SessionState::kOnline;
  session.player_id = player_id;
  player_conns_[player_id] = conn;

  // 先入队 LoginResp，再让 World 下发 SceneInfo，保证客户端先拿到登录结果
  proto::LoginResp resp;
  resp.ok = 1;
  resp.player_id = player_id;
  QueueSend(conn, proto::kLoginResp, packet.seq, proto::EncodeLoginResp(resp));
  world_.EnterGame(player_id);
}

void GameServer::HandleLogout(Connection *conn, Session &session,
                              const proto::Packet &packet) {
  if (session.state != SessionState::kOnline) {
    QueueSend(conn, proto::kLogoutResp, packet.seq, "");
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const uint64_t player_id = session.player_id;
  world_.Logout(player_id);
  auto it = player_conns_.find(player_id);
  if (it != player_conns_.end() && it->second == conn) {
    player_conns_.erase(it);
  }

  session.state = SessionState::kUnauthenticated;
  session.player_id = 0;
  QueueSend(conn, proto::kLogoutResp, packet.seq, "");
}

void GameServer::QueueSend(Connection *conn, uint16_t msg_id, uint32_t seq,
                           const std::string &body) {
  if (conn == nullptr) {
    return;
  }
  std::string packet = proto::EncodePacket(msg_id, seq, body);
  EventLoop *loop = conn->GetLoop();
  if (loop == nullptr) {
    return;
  }

  // 生命周期说明：连接析构同样以任务形式投递到该 loop，且关闭回调会先于
  // 析构任务执行并清空 GameServer 中的映射；任务 FIFO 保证这里的 send
  // 一定在 delete 之前执行。连接状态只在 loop 线程内读取。
  loop->AddTask([conn, packet = std::move(packet)]() {
    if (conn->GetState() == Connection::Connected) {
      conn->Send(packet);
    }
  });
}

void GameServer::SendErrorTo(Connection *conn, uint32_t seq, uint16_t code,
                             const std::string &message) {
  proto::ErrorNotify notify;
  notify.code = code;
  notify.message = message;
  QueueSend(conn, proto::kErrorNotify, seq, proto::EncodeErrorNotify(notify));
}

void GameServer::SendToPlayerLocked(uint64_t player_id, uint16_t msg_id,
                                    const std::string &body) {
  auto it = player_conns_.find(player_id);
  if (it == player_conns_.end() || it->second == nullptr) {
    return;
  }
  QueueSend(it->second, msg_id, server_seq_.fetch_add(1), body);
}
