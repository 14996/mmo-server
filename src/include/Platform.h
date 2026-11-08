/**
 * @file Platform.h
 * @brief 跨平台 socket 基础封装。
 *
 * 生产环境（Linux）走 epoll + 非阻塞 socket；
 * Windows 仅作为本地开发环境，走 WSAPoll，接口保持一致。
 */
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef FD_SETSIZE
#define FD_SETSIZE 1024
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <sys/types.h>

using SocketHandle = SOCKET;
using SockLen = int;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

inline bool SocketInit() {
  WSADATA data;
  return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}
inline void SocketCleanup() { WSACleanup(); }
inline int LastSocketError() { return WSAGetLastError(); }
inline bool IsWouldBlock(int err) { return err == WSAEWOULDBLOCK; }
inline bool IsInterrupted(int err) { return err == WSAEINTR; }
inline bool IsConnectInProgress(int err) {
  return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS || err == WSAEALREADY;
}
inline bool IsConnectionReset(int err) {
  return err == WSAECONNRESET || err == WSAECONNABORTED ||
         err == WSAENOTCONN || err == WSAESHUTDOWN;
}
inline void CloseSocketHandle(SocketHandle fd) {
  if (fd != kInvalidSocket) {
    closesocket(fd);
  }
}
inline bool SetSocketNonBlocking(SocketHandle fd, bool on) {
  u_long mode = on ? 1 : 0;
  return ioctlsocket(fd, FIONBIO, &mode) == 0;
}
inline ssize_t SocketRead(SocketHandle fd, char *buf, int len) {
  return recv(fd, buf, len, 0);
}
inline ssize_t SocketWrite(SocketHandle fd, const char *buf, int len) {
  return send(fd, buf, len, 0);
}
inline void SleepMs(int ms) { Sleep(static_cast<DWORD>(ms)); }
inline void IgnoreSigPipe() {}

#else  // POSIX

#include <arpa/inet.h>
#include <csignal>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

using SocketHandle = int;
using SockLen = socklen_t;
constexpr SocketHandle kInvalidSocket = -1;

inline bool SocketInit() { return true; }
inline void SocketCleanup() {}
inline int LastSocketError() { return errno; }
inline bool IsWouldBlock(int err) { return err == EAGAIN || err == EWOULDBLOCK; }
inline bool IsInterrupted(int err) { return err == EINTR; }
inline bool IsConnectInProgress(int err) { return err == EINPROGRESS; }
inline bool IsConnectionReset(int err) {
  return err == ECONNRESET || err == ECONNABORTED || err == ENOTCONN ||
         err == ESHUTDOWN;
}
inline void CloseSocketHandle(SocketHandle fd) {
  if (fd != kInvalidSocket) {
    ::close(fd);
  }
}
inline bool SetSocketNonBlocking(SocketHandle fd, bool on) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }
  flags = on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
  return fcntl(fd, F_SETFL, flags) == 0;
}
inline ssize_t SocketRead(SocketHandle fd, char *buf, int len) {
  return ::read(fd, buf, len);
}
inline ssize_t SocketWrite(SocketHandle fd, const char *buf, int len) {
  return ::write(fd, buf, len);
}
inline void SleepMs(int ms) {
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000L;
  nanosleep(&ts, nullptr);
}
inline void IgnoreSigPipe() {
  signal(SIGPIPE, SIG_IGN);
}

#endif
