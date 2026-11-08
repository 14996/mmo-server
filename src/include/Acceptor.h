/**
 * @file Acceptor.h
 * @brief 监听 socket：接受新连接并交给 Server
 */
#pragma once

#include <cstdint>
#include <functional>

#include "Macros.h"
#include "Platform.h"

class EventLoop;
class Socket;
class Channel;
class InetAddress;

class Acceptor {
 public:
  explicit Acceptor(EventLoop *loop, const char *ip = "0.0.0.0", uint16_t port = 1234);
  ~Acceptor();

  DISALLOW_COPY_AND_MOVE(Acceptor);

  void AcceptConnection();
  void SetNewConnectionCallback(std::function<void(Socket *)> const &callback);

 private:
  EventLoop *loop_;
  Socket *sock_;
  Channel *channel_;
  std::function<void(Socket *)> new_connection_callback_;
};
