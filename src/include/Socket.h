/**
 * @file Socket.h
 * @brief socket 与地址封装（跨平台）
 */
#pragma once

#include <cstdint>

#include "Macros.h"
#include "Platform.h"

class InetAddress {
 public:
  InetAddress();
  InetAddress(const char *ip, uint16_t port);
  ~InetAddress() = default;

  DISALLOW_COPY_AND_MOVE(InetAddress);

  void SetAddr(const sockaddr_in &addr);
  sockaddr_in GetAddr() const;
  const char *GetIp();
  uint16_t GetPort() const;

  /// 构造时 IP 解析是否成功（accept 得到的地址恒为 true）
  bool IsValid() const { return valid_; }

 private:
  sockaddr_in addr_{};
  char ip_text_[INET_ADDRSTRLEN]{};
  bool valid_{true};
};

class Socket {
 public:
  Socket();
  explicit Socket(SocketHandle fd);
  ~Socket();

  DISALLOW_COPY_AND_MOVE(Socket);

  void Bind(InetAddress *addr);
  void Listen();
  SocketHandle Accept(InetAddress *addr);

  void Connect(InetAddress *addr);
  void Connect(const char *ip, uint16_t port);

  void SetNonBlocking(bool on = true);
  bool IsNonBlocking() const;
  void SetNoDelay();
  SocketHandle GetFd() const;

 private:
  SocketHandle fd_{kInvalidSocket};
  bool nonblocking_{false};
};
