/**
 * @file Poller.cpp
 */
#include "Poller.h"

#include <cerrno>
#include <cstring>

#include "Channel.h"
#include "util.h"

#ifdef _WIN32

#include <algorithm>

Poller::Poller() { pollfds_.reserve(64); }

Poller::~Poller() = default;

void Poller::UpdateChannel(Channel *channel) {
  SocketHandle fd = channel->GetFd();
  if (channel->GetListenEvents() == 0) {
    auto it = channels_.find(fd);
    if (it != channels_.end()) {
      channels_.erase(it);
      channel->SetInPoller(false);
    }
    return;
  }
  channels_[fd] = channel;
  channel->SetInPoller(true);
}

void Poller::DeleteChannel(Channel *channel) {
  auto it = channels_.find(channel->GetFd());
  if (it != channels_.end()) {
    channels_.erase(it);
  }
  channel->SetInPoller(false);
}

std::vector<Channel *> Poller::Poll(int timeout_ms) {
  std::vector<Channel *> active;
  if (channels_.empty()) {
    SleepMs(timeout_ms < 0 ? 10 : timeout_ms);
    return active;
  }

  pollfds_.clear();
  for (auto &item : channels_) {
    WSAPOLLFD pfd{};
    pfd.fd = item.first;
    Channel *channel = item.second;
    if (channel->GetListenEvents() & Channel::kReadEvent) {
      pfd.events |= POLLRDNORM;
    }
    if (channel->GetListenEvents() & Channel::kWriteEvent) {
      pfd.events |= POLLWRNORM;
    }
    pollfds_.push_back(pfd);
  }

  int n = WSAPoll(pollfds_.data(), static_cast<ULONG>(pollfds_.size()), timeout_ms);
  if (n == SOCKET_ERROR) {
    int err = LastSocketError();
    if (err != WSAEINTR) {
      LogError("WSAPoll error=%d", err);
    }
    return active;
  }

  for (auto &pfd : pollfds_) {
    if (pfd.revents == 0) {
      continue;
    }
    auto it = channels_.find(pfd.fd);
    if (it == channels_.end()) {
      continue;
    }
    uint32_t events = 0;
    if (pfd.revents & (POLLRDNORM | POLLRDBAND)) {
      events |= Channel::kReadEvent;
    }
    if (pfd.revents & (POLLWRNORM | POLLWRBAND)) {
      events |= Channel::kWriteEvent;
    }
    if (pfd.revents & POLLERR) {
      events |= Channel::kErrorEvent;
    }
    if (pfd.revents & POLLHUP) {
      events |= Channel::kHupEvent;
    }
    if (pfd.revents & POLLNVAL) {
      events |= Channel::kErrorEvent;
    }
    if (events == 0) {
      continue;
    }
    it->second->SetReadyEvents(events);
    active.push_back(it->second);
  }
  return active;
}

#else  // Linux epoll

#include <sys/epoll.h>
#include <unistd.h>

namespace {
constexpr int kMaxEvents = 1024;
}

Poller::Poller() {
  epfd_ = epoll_create1(0);
  ErrorIf(epfd_ == -1, "epoll_create1 error");
  events_.resize(kMaxEvents);
}

Poller::~Poller() {
  if (epfd_ != -1) {
    close(epfd_);
    epfd_ = -1;
  }
}

void Poller::UpdateChannel(Channel *channel) {
  int fd = channel->GetFd();
  uint32_t listen_events = channel->GetListenEvents();
  if (listen_events == 0) {
    if (channel->GetInPoller()) {
      epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
      channel->SetInPoller(false);
    }
    return;
  }

  struct epoll_event ev {};
  ev.data.ptr = channel;
  ev.events = 0;
  if (listen_events & Channel::kReadEvent) {
    ev.events |= EPOLLIN | EPOLLRDHUP;
  }
  if (listen_events & Channel::kWriteEvent) {
    ev.events |= EPOLLOUT;
  }

  int op = channel->GetInPoller() ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
  if (epoll_ctl(epfd_, op, fd, &ev) == -1) {
    LogError("epoll_ctl op=%d fd=%d error=%d", op, fd, errno);
  } else {
    channel->SetInPoller(true);
  }
}

void Poller::DeleteChannel(Channel *channel) {
  if (!channel->GetInPoller()) {
    return;
  }
  if (epoll_ctl(epfd_, EPOLL_CTL_DEL, channel->GetFd(), nullptr) == -1) {
    LogError("epoll_ctl DEL fd=%d error=%d", channel->GetFd(), errno);
  }
  channel->SetInPoller(false);
}

std::vector<Channel *> Poller::Poll(int timeout_ms) {
  std::vector<Channel *> active;
  int nfds = 0;
  do {
    nfds = epoll_wait(epfd_, events_.data(), kMaxEvents, timeout_ms);
  } while (nfds == -1 && errno == EINTR);

  if (nfds == -1) {
    LogError("epoll_wait error=%d", errno);
    return active;
  }

  for (int i = 0; i < nfds; ++i) {
    auto *channel = static_cast<Channel *>(events_[i].data.ptr);
    uint32_t events = 0;
    uint32_t raw = events_[i].events;
    if (raw & (EPOLLIN | EPOLLPRI)) {
      events |= Channel::kReadEvent;
    }
    if (raw & EPOLLOUT) {
      events |= Channel::kWriteEvent;
    }
    if (raw & EPOLLERR) {
      events |= Channel::kErrorEvent;
    }
    if (raw & (EPOLLHUP | EPOLLRDHUP)) {
      events |= Channel::kHupEvent;
    }
    channel->SetReadyEvents(events);
    active.push_back(channel);
  }
  return active;
}

#endif
