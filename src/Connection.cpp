/**
 * @file Connection.cpp
 */
#include "Connection.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

#include "Channel.h"
#include "EventLoop.h"
#include "Socket.h"
#include "util.h"

Connection::Connection(EventLoop *loop, Socket *sock) : loop_(loop), sock_(sock) {
  state_ = State::Connected;
  channel_ = new Channel(loop_, sock_->GetFd());
  channel_->SetReadCallback([this]() { HandleRead(); });
  channel_->SetWriteCallback([this]() { HandleWrite(); });
}

Connection::~Connection() {
  delete channel_;
  channel_ = nullptr;
  delete sock_;
  sock_ = nullptr;
}

void Connection::EnableRead() {
  if (channel_ == nullptr || state_ != State::Connected) {
    return;
  }
  channel_->EnableRead();
}

void Connection::Send(const std::string &data) {
  if (state_ != State::Connected || data.empty()) {
    return;
  }
  if (send_buffer_.Size() + data.size() > kMaxSendBufferBytes) {
    LogWarn("send buffer overflow (%zu bytes), closing connection",
            send_buffer_.Size() + data.size());
    Close();
    return;
  }
  send_buffer_.Append(data);
  WriteNonBlocking();
}

void Connection::Close() {
  // 注意：读/写错误路径会先把 state_ 置为 Closed 再调用 Close，
  // 因此不能只判断 state_，否则业务层关闭回调永远不会触发。
  if (close_notified_) {
    return;
  }
  close_notified_ = true;
  state_ = State::Closed;

  if (channel_ != nullptr) {
    channel_->DisableAll();
  }

  // 先通知业务层（清理玩家 / 保存数据），再通知 Server 调度析构
  if (close_callback_) {
    close_callback_(this);
  }
  if (delete_connection_callback_) {
    delete_connection_callback_(sock_);
  }
}

void Connection::HandleRead() {
  if (state_ == State::Closed && close_notified_) {
    return;
  }
  if (state_ != State::Connected) {
    Close();
    return;
  }

  ReadNonBlocking();
  const bool peer_closed = state_ != State::Connected;

  // 对端可能在一帧数据之后紧跟着 FIN：先把读到的完整帧交给业务层，
  // 再执行关闭回调，避免丢掉最后一条消息（例如 LogoutReq）。
  if (message_callback_ && read_buffer_.Size() > 0) {
    message_callback_(this);
  }
  if (peer_closed) {
    Close();
  }
}

void Connection::HandleWrite() {
  if (state_ != State::Connected) {
    return;
  }
  WriteNonBlocking();
}

void Connection::ReadNonBlocking() {
  char buf[16384];
  SocketHandle fd = sock_->GetFd();
  while (true) {
    ssize_t n = SocketRead(fd, buf, static_cast<int>(sizeof(buf)));
    if (n > 0) {
      read_buffer_.Append(buf, static_cast<size_t>(n));
      continue;
    }
    if (n == 0) {
      state_ = State::Closed;
      return;
    }
    int err = LastSocketError();
    if (IsInterrupted(err)) {
      continue;
    }
    if (IsWouldBlock(err)) {
      return;
    }
    if (!IsConnectionReset(err)) {
      LogWarn("connection read error=%d", err);
    }
    state_ = State::Closed;
    return;
  }
}

void Connection::WriteNonBlocking() {
  SocketHandle fd = sock_->GetFd();

  while (send_buffer_.Size() > 0) {
    const size_t chunk = std::min(
        send_buffer_.ReadableBytes(),
        static_cast<size_t>(std::numeric_limits<int>::max()));
    ssize_t n = SocketWrite(fd, send_buffer_.ReadPtr(), static_cast<int>(chunk));
    if (n > 0) {
      send_buffer_.Retrieve(static_cast<size_t>(n));
      continue;
    }
    if (n == 0) {
      state_ = State::Closed;
      break;
    }
    int err = LastSocketError();
    if (IsInterrupted(err)) {
      continue;
    }
    if (IsWouldBlock(err)) {
      break;
    }
    if (!IsConnectionReset(err)) {
      LogWarn("connection write error=%d", err);
    }
    state_ = State::Closed;
    break;
  }

  if (state_ != State::Connected) {
    Close();
    return;
  }

  if (send_buffer_.Size() > 0) {
    channel_->EnableWrite();
  } else {
    channel_->DisableWrite();
  }
}

Connection::State Connection::GetState() const { return state_; }

Socket *Connection::GetSocket() { return sock_; }

EventLoop *Connection::GetLoop() { return loop_; }

Buffer *Connection::GetReadBuffer() { return &read_buffer_; }

void Connection::SetMessageCallback(std::function<void(Connection *)> const &callback) {
  message_callback_ = callback;
}

void Connection::SetCloseCallback(std::function<void(Connection *)> const &callback) {
  close_callback_ = callback;
}

void Connection::SetDeleteConnectionCallback(std::function<void(Socket *)> const &callback) {
  delete_connection_callback_ = callback;
}
