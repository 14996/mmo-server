/**
 * @file Connection.h
 * @brief TCP 连接：读缓冲、可写事件驱动的发送队列、生命周期回调
 */
#pragma once

#include <functional>
#include <string>

#include "Buffer.h"
#include "Macros.h"
#include "Platform.h"

class EventLoop;
class Socket;
class Channel;

class Connection {
 public:
  enum State {
    Invalid = 1,
    Connected,
    Closed,
  };

  Connection(EventLoop *loop, Socket *sock);
  ~Connection();

  DISALLOW_COPY_AND_MOVE(Connection);

  /// 在连接所属 EventLoop 线程中调用
  void EnableRead();

  /// 在连接所属 EventLoop 线程中调用；内部会处理写不完的剩余数据
  void Send(const std::string &data);

  /// 在连接所属 EventLoop 线程中调用
  void Close();

  State GetState() const;
  Socket *GetSocket();
  EventLoop *GetLoop();
  Buffer *GetReadBuffer();

  void SetMessageCallback(std::function<void(Connection *)> const &callback);
  void SetCloseCallback(std::function<void(Connection *)> const &callback);
  void SetDeleteConnectionCallback(std::function<void(Socket *)> const &callback);

 private:
  void HandleRead();
  void HandleWrite();
  void ReadNonBlocking();
  void WriteNonBlocking();

  EventLoop *loop_;
  Socket *sock_;
  Channel *channel_{nullptr};
  State state_{State::Invalid};
  bool close_notified_{false};
  Buffer read_buffer_;
  Buffer send_buffer_;

  std::function<void(Connection *)> message_callback_;
  std::function<void(Connection *)> close_callback_;
  std::function<void(Socket *)> delete_connection_callback_;
};
