/**
 * @file Acceptor.cpp
 */
#include "Acceptor.h"

#include <cstdio>
#include <utility>

#include "Channel.h"
#include "Socket.h"
#include "util.h"

Acceptor::Acceptor(EventLoop *loop, const char *ip, uint16_t port)
    : loop_(loop), sock_(nullptr), channel_(nullptr) {
  sock_ = new Socket();
  sock_->Bind(new InetAddress(ip, port));
  sock_->Listen();
  channel_ = new Channel(loop_, sock_->GetFd());
  channel_->SetReadCallback([this]() { AcceptConnection(); });
  channel_->EnableRead();
  LogInfo("acceptor listening on %s:%u", ip, static_cast<unsigned>(port));
}

Acceptor::~Acceptor() {
  delete channel_;
  channel_ = nullptr;
  delete sock_;
  sock_ = nullptr;
}

void Acceptor::AcceptConnection() {
  InetAddress client_addr;
  SocketHandle client_fd = sock_->Accept(&client_addr);

  if (client_fd != kInvalidSocket) {
    Socket *client_sock = new Socket(client_fd);
    client_sock->SetNonBlocking(true);
    client_sock->SetNoDelay();
    LogInfo("new client fd=%llu ip=%s port=%u",
            static_cast<unsigned long long>(client_fd), client_addr.GetIp(),
            static_cast<unsigned>(client_addr.GetPort()));
    if (new_connection_callback_) {
      new_connection_callback_(client_sock);
    } else {
      delete client_sock;
    }
  }
}

void Acceptor::SetNewConnectionCallback(std::function<void(Socket *)> const &callback) {
  new_connection_callback_ = callback;
}
