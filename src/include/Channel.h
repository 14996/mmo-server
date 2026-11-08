/**
 * @file Channel.h
 * @brief fd + 关注事件 + 回调，是 Reactor 的最小单元
 */
#pragma once

#include <cstdint>
#include <functional>

#include "Macros.h"
#include "Platform.h"

class EventLoop;

class Channel {
 public:
  /// 可移植的事件位，由 Poller 翻译成 epoll / WSAPoll 的具体事件
  enum EventBits : uint32_t {
    kReadEvent = 0x1,
    kWriteEvent = 0x2,
    kErrorEvent = 0x4,
    kHupEvent = 0x8,
  };

  Channel(EventLoop *loop, SocketHandle fd);
  ~Channel();

  DISALLOW_COPY_AND_MOVE(Channel);

  void HandleEvent();
  void EnableRead();
  void EnableWrite();
  void DisableWrite();
  void DisableAll();
  void Remove();

  SocketHandle GetFd() const;
  uint32_t GetListenEvents() const;
  uint32_t GetReadyEvents() const;
  bool GetInPoller() const;
  void SetInPoller(bool in = true);
  void SetReadyEvents(uint32_t events);

  void SetReadCallback(std::function<void()> const &callback);
  void SetWriteCallback(std::function<void()> const &callback);

 private:
  EventLoop *loop_;
  SocketHandle fd_;
  uint32_t listen_events_{0};
  uint32_t ready_events_{0};
  bool in_poller_{false};
  std::function<void()> read_callback_;
  std::function<void()> write_callback_;
};
