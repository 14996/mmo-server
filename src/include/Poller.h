/**
 * @file Poller.h
 * @brief IO 多路复用抽象：Linux 用 epoll，Windows 开发环境用 WSAPoll
 */
#pragma once

#include <map>
#include <vector>

#include "Macros.h"
#include "Platform.h"

class Channel;

class Poller {
 public:
  Poller();
  ~Poller();

  DISALLOW_COPY_AND_MOVE(Poller);

  void UpdateChannel(Channel *channel);
  void DeleteChannel(Channel *channel);

  /// timeout_ms < 0 表示一直阻塞直到有事件
  std::vector<Channel *> Poll(int timeout_ms = -1);

 private:
#ifdef _WIN32
  std::map<SocketHandle, Channel *> channels_;
  std::vector<WSAPOLLFD> pollfds_;
#else
  int epfd_{-1};
  std::vector<struct epoll_event> events_;
#endif
};
