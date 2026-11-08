/**
 * @file Socket.cpp
 */
#include "Socket.h"

#include <cstring>

#include "util.h"

Socket::Socket() {
  fd_ = socket(AF_INET, SOCK_STREAM, 0);
  ErrorIf(fd_ == kInvalidSocket, "socket create error");
}

Socket::Socket(SocketHandle fd) : fd_(fd) {
  ErrorIf(fd_ == kInvalidSocket, "socket create error");
}

Socket::~Socket() {
  if (fd_ != kInvalidSocket) {
    CloseSocketHandle(fd_);
    fd_ = kInvalidSocket;
  }
}

void Socket::Bind(InetAddress *addr) {
  ErrorIf(addr == nullptr || !addr->IsValid(), "invalid bind address");
  sockaddr_in raw = addr->GetAddr();
  int reuse = 1;
  setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse), sizeof(reuse));
  int ret = bind(fd_, reinterpret_cast<sockaddr *>(&raw), sizeof(raw));
  ErrorIf(ret == -1, "socket bind error");
}

void Socket::Listen() {
  int ret = listen(fd_, SOMAXCONN);
  ErrorIf(ret == -1, "socket listen error");
}

SocketHandle Socket::Accept(InetAddress *addr) {
  sockaddr_in raw{};
  SockLen addr_len = sizeof(raw);
  SocketHandle client_fd = kInvalidSocket;

  if (nonblocking_) {
    while (true) {
      client_fd = accept(fd_, reinterpret_cast<sockaddr *>(&raw), &addr_len);
      if (client_fd == kInvalidSocket && IsWouldBlock(LastSocketError())) {
        return kInvalidSocket;
      }
      if (client_fd == kInvalidSocket) {
        int err = LastSocketError();
        if (IsInterrupted(err)) {
          continue;
        }
        // accept 失败（如 EMFILE）不应直接终止进程，记录后等待下次事件
        LogWarn("socket accept error=%d", err);
        return kInvalidSocket;
      }
      break;
    }
  } else {
    client_fd = accept(fd_, reinterpret_cast<sockaddr *>(&raw), &addr_len);
    if (client_fd == kInvalidSocket) {
      LogWarn("socket accept error=%d", LastSocketError());
      return kInvalidSocket;
    }
  }

  if (addr != nullptr) {
    addr->SetAddr(raw);
  }
  return client_fd;
}

void Socket::Connect(InetAddress *addr) {
  ErrorIf(addr == nullptr || !addr->IsValid(), "invalid connect address");
  sockaddr_in raw = addr->GetAddr();
  int ret = connect(fd_, reinterpret_cast<sockaddr *>(&raw), sizeof(raw));
  if (ret == 0) {
    return;
  }
  int err = LastSocketError();
  if (nonblocking_ && IsConnectInProgress(err)) {
    // 非阻塞 connect 尚未完成，交给调用方后续用可写事件判断
    return;
  }
  ErrorIf(true, "socket connect error");
}

void Socket::Connect(const char *ip, uint16_t port) {
  InetAddress addr(ip, port);
  Connect(&addr);
}

void Socket::SetNonBlocking(bool on) {
  if (!SetSocketNonBlocking(fd_, on)) {
    ErrorIf(true, "set socket nonblocking error");
  }
  nonblocking_ = on;
}

bool Socket::IsNonBlocking() const { return nonblocking_; }

void Socket::SetNoDelay() {
  int flag = 1;
  if (setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY,
                 reinterpret_cast<const char *>(&flag), sizeof(flag)) != 0) {
    LogWarn("setsockopt TCP_NODELAY failed: error=%d", LastSocketError());
  }
}

SocketHandle Socket::GetFd() const { return fd_; }

InetAddress::InetAddress() = default;

InetAddress::InetAddress(const char *ip, uint16_t port) {
  std::memset(&addr_, 0, sizeof(addr_));
  addr_.sin_family = AF_INET;
  addr_.sin_port = htons(port);
  if (ip == nullptr) {
    valid_ = false;
    return;
  }
#ifdef _WIN32
  valid_ = InetPtonA(AF_INET, ip, &addr_.sin_addr) == 1;
#else
  valid_ = inet_pton(AF_INET, ip, &addr_.sin_addr) == 1;
#endif
}

void InetAddress::SetAddr(const sockaddr_in &addr) { addr_ = addr; }

sockaddr_in InetAddress::GetAddr() const { return addr_; }

const char *InetAddress::GetIp() {
#ifdef _WIN32
  InetNtopA(AF_INET, &addr_.sin_addr, ip_text_, INET_ADDRSTRLEN);
#else
  inet_ntop(AF_INET, &addr_.sin_addr, ip_text_, INET_ADDRSTRLEN);
#endif
  return ip_text_;
}

uint16_t InetAddress::GetPort() const { return ntohs(addr_.sin_port); }
