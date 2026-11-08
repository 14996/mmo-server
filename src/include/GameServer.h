/**
 * @file GameServer.h
 * @brief 网络层与游戏世界之间的胶水层：会话管理、协议分发、定时 tick
 *
 * 线程模型：
 *   - 每个连接的消息回调运行在它所属的 sub reactor 线程；
 *   - tick 线程以 50ms 为周期驱动 World::Tick；
 *   - World / 会话表 / playerId->Connection 映射统一由 mutex_ 保护；
 *   - 所有下行包都通过 EventLoop::AddTask 投递回连接所属的 reactor 发送，
 *     避免在游戏锁内做 socket I/O。
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "Macros.h"
#include "Protocol.h"
#include "World.h"

class Server;
class Connection;

class GameServer {
 public:
  explicit GameServer(Server *server);
  ~GameServer();

  DISALLOW_COPY_AND_MOVE(GameServer);

  void Start();
  void Stop();

 private:
  struct Session {
    uint64_t player_id{0};
    bool authed{false};
    std::string parse_buffer;
  };

  void TickLoop();
  void OnNewConnection(Connection *conn);
  void OnMessage(Connection *conn);
  void OnConnectionClose(Connection *conn);

  void Dispatch(Connection *conn, Session &session, const proto::Packet &packet);
  void HandleLogin(Connection *conn, Session &session, const proto::Packet &packet);
  void HandleLogout(Connection *conn, Session &session, const proto::Packet &packet);

  /// 把响应/推送投递到目标连接所属的 EventLoop（线程安全）
  void QueueSend(Connection *conn, uint16_t msg_id, uint32_t seq,
                 const std::string &body);
  void SendErrorTo(Connection *conn, uint32_t seq, uint16_t code,
                   const std::string &message);

  /// 仅由 World 在持有 mutex_ 时调用
  void SendToPlayerLocked(uint64_t player_id, uint16_t msg_id,
                          const std::string &body);

  Server *server_;
  std::mutex mutex_;
  std::unordered_map<Connection *, Session> sessions_;
  std::unordered_map<uint64_t, Connection *> player_conns_;
  World world_;
  std::thread tick_thread_;
  std::atomic<bool> running_{false};
  std::atomic<uint32_t> server_seq_{1};
};
