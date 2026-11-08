/**
 * @file Channel.cpp
 */
#include "Channel.h"

#include <utility>

#include "EventLoop.h"

Channel::Channel(EventLoop *loop, SocketHandle fd) : loop_(loop), fd_(fd) {}

Channel::~Channel() {
  // fd 的所有权属于 Socket，Channel 只负责从 Poller 注销
  if (loop_ != nullptr && in_poller_) {
    loop_->DeleteChannel(this);
    in_poller_ = false;
  }
}

void Channel::HandleEvent() {
  uint32_t events = ready_events_;
  // EPOLLERR / EPOLLHUP / POLLHUP 都交给读回调处理，读操作会返回 0 或错误并触发关闭
  if ((events & (kReadEvent | kErrorEvent | kHupEvent)) && read_callback_) {
    read_callback_();
  }
  if ((events & kWriteEvent) && write_callback_) {
    write_callback_();
  }
}

void Channel::EnableRead() {
  listen_events_ |= kReadEvent;
  loop_->UpdateChannel(this);
}

void Channel::EnableWrite() {
  listen_events_ |= kWriteEvent;
  loop_->UpdateChannel(this);
}

void Channel::DisableWrite() {
  listen_events_ &= ~kWriteEvent;
  loop_->UpdateChannel(this);
}

void Channel::DisableAll() {
  listen_events_ = 0;
  loop_->UpdateChannel(this);
}

void Channel::Remove() {
  if (loop_ != nullptr) {
    loop_->DeleteChannel(this);
  }
}

SocketHandle Channel::GetFd() const { return fd_; }

uint32_t Channel::GetListenEvents() const { return listen_events_; }

uint32_t Channel::GetReadyEvents() const { return ready_events_; }

bool Channel::GetInPoller() const { return in_poller_; }

void Channel::SetInPoller(bool in) { in_poller_ = in; }

void Channel::SetReadyEvents(uint32_t events) { ready_events_ = events; }

void Channel::SetReadCallback(std::function<void()> const &callback) { read_callback_ = callback; }

void Channel::SetWriteCallback(std::function<void()> const &callback) { write_callback_ = callback; }
