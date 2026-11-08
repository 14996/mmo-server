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

GameServer::GameServer(Server *server)
    : server_(server),
      world_([this](uint64_t player_id, uint16_t msg_id,
                    const std::string &body) {
        SendToPlayerLocked(player_id, msg_id, body);
      }) {
  ASSERT(server_ != nullptr, "server must not be null");
}

GameServer::~GameServer() { Stop(); }

void GameServer::Start() {
  if (running_.exchange(true)) {
    return;
  }
  server_->SetOnNewConnection(
      [this](Connection *conn) { OnNewConnection(conn); });
  server_->SetMessageCallback([this](Connection *conn) { OnMessage(conn); });
  tick_thread_ = std::thread([this]() { TickLoop(); });
  LogInfo("game server started (tick interval 50ms)");
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
    {
      std::lock_guard<std::mutex> lock(mutex_);
      world_.Tick(NowMs());
    }
    SleepMs(50);
  }
}

void GameServer::OnNewConnection(Connection *conn) {
  conn->SetMessageCallback([this](Connection *c) { OnMessage(c); });
  conn->SetCloseCallback([this](Connection *c) { OnConnectionClose(c); });

  std::lock_guard<std::mutex> lock(mutex_);
  sessions_[conn] = Session{};
}

void GameServer::OnConnectionClose(Connection *conn) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = sessions_.find(conn);
  if (it == sessions_.end()) {
    return;
  }

  if (it->second.authed && it->second.player_id != 0) {
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
    if (it == sessions_.end()) {
      return;
    }
    session = &it->second;
  }

  Buffer *read_buffer = conn->GetReadBuffer();
  const size_t n = read_buffer->Size();
  if (n > 0) {
    session->parse_buffer.append(read_buffer->Data(), n);
    read_buffer->Clear();
  }

  if (session->parse_buffer.size() > proto::kMaxPendingBytes) {
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
    if (status == proto::DecodeStatus::kError) {
      LogWarn("invalid frame header, closing connection");
      conn->Close();
      return;
    }
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

  if (!session.authed) {
    SendErrorTo(conn, packet.seq, 401, "请先登录");
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
  if (session.authed) {
    proto::LoginResp resp;
    resp.ok = 0;
    resp.error = "当前连接已经登录";
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
  std::string error;
  bool is_new = false;
  uint64_t player_id = world_.Login(req.name, req.password, error, is_new);
  if (player_id == 0) {
    proto::LoginResp resp;
    resp.ok = 0;
    resp.error = error;
    QueueSend(conn, proto::kLoginResp, packet.seq, proto::EncodeLoginResp(resp));
    return;
  }

  session.authed = true;
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
  if (!session.authed) {
    QueueSend(conn, proto::kLogoutResp, packet.seq, "");
    return;
  }

  const uint64_t player_id = session.player_id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    world_.Logout(player_id);
    auto it = player_conns_.find(player_id);
    if (it != player_conns_.end() && it->second == conn) {
      player_conns_.erase(it);
    }
  }

  session.authed = false;
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
