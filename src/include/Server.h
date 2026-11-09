/**
 * @file Server.h
 * @brief 主从 Reactor：主线程 accept，sub reactor 处理连接读写
 */
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

#include "Macros.h"
#include "Platform.h"

class EventLoop;
class Socket;
class Acceptor;
class Connection;
class ThreadPool;

class Server {
 public:
  explicit Server(EventLoop *loop, const char *ip = "0.0.0.0", uint16_t port = 9527);
  ~Server();

  DISALLOW_COPY_AND_MOVE(Server);

  void NewConnection(Socket *sock);
  void DeleteConnection(Socket *sock);

  using ConnectionCallback = std::function<void(Connection *)>;

  /// 连接上有数据可读时触发（在连接所属的 sub reactor 线程中调用）
  void SetMessageCallback(ConnectionCallback fn);

  /// 新连接建立时触发一次（同样在 sub reactor 线程，便于直接 EnableRead）
  void SetOnNewConnection(ConnectionCallback fn);

  size_t SubReactorCount() const { return sub_reactors_.size(); }

 private:
  EventLoop *main_reactor_;
  Acceptor *acceptor_;
  std::map<SocketHandle, Connection *> connections_;
  std::mutex connections_mutex_;
  std::vector<EventLoop *> sub_reactors_;
  ThreadPool *thread_pool_;
  ConnectionCallback message_callback_;
  ConnectionCallback new_connection_callback_;
};
