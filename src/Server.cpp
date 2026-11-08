/**
 * @file Server.cpp
 */
#include "Server.h"

#include <algorithm>
#include <thread>
#include <utility>

#include "Acceptor.h"
#include "Connection.h"
#include "EventLoop.h"
#include "Socket.h"
#include "ThreadPool.h"
#include "util.h"

Server::Server(EventLoop *loop, const char *ip, uint16_t port)
    : main_reactor_(loop), acceptor_(nullptr), thread_pool_(nullptr) {
  acceptor_ = new Acceptor(main_reactor_, ip, port);
  acceptor_->SetNewConnectionCallback(
      [this](Socket *sock) { NewConnection(sock); });

  unsigned int count = std::thread::hardware_concurrency();
  if (count == 0) {
    count = 1;
  }
  count = std::min<unsigned int>(count, 4);
  thread_pool_ = new ThreadPool(count);
  for (unsigned int i = 0; i < count; ++i) {
    sub_reactors_.push_back(new EventLoop());
  }
  for (unsigned int i = 0; i < count; ++i) {
    EventLoop *sub_loop = sub_reactors_[i];
    thread_pool_->Add([sub_loop]() { sub_loop->Loop(); });
  }
}

Server::~Server() {
  delete acceptor_;
  acceptor_ = nullptr;

  for (auto *loop : sub_reactors_) {
    loop->Quit();
  }
  delete thread_pool_;
  thread_pool_ = nullptr;

  // 此时 sub reactor 已全部退出，可以安全释放剩余连接
  for (auto &item : connections_) {
    delete item.second;
  }
  connections_.clear();

  for (auto *loop : sub_reactors_) {
    delete loop;
  }
  sub_reactors_.clear();
}

void Server::NewConnection(Socket *sock) {
  if (sock == nullptr || sock->GetFd() == kInvalidSocket) {
    delete sock;
    return;
  }
  if (sub_reactors_.empty()) {
    delete sock;
    return;
  }

  size_t index = static_cast<size_t>(sock->GetFd()) % sub_reactors_.size();
  EventLoop *sub_loop = sub_reactors_[index];
  Connection *conn = new Connection(sub_loop, sock);
  conn->SetDeleteConnectionCallback(
      [this](Socket *socket) { DeleteConnection(socket); });

  if (message_callback_) {
    conn->SetMessageCallback(message_callback_);
  }

  SocketHandle fd = sock->GetFd();

  // 连接的所有 epoll 操作都放到其所属 reactor 线程执行，避免跨线程操作 poller
  sub_loop->AddTask([this, conn, fd, sub_loop]() {
    {
      std::lock_guard<std::mutex> lock(connections_mutex_);
      connections_[fd] = conn;
    }
    if (new_connection_callback_) {
      new_connection_callback_(conn);
    }
    {
      std::lock_guard<std::mutex> lock(connections_mutex_);
      if (connections_.find(fd) == connections_.end()) {
        return;  // 新连接回调里已经把连接关掉了
      }
    }
    conn->EnableRead();
  });

  (void)sub_loop;
}

void Server::DeleteConnection(Socket *sock) {
  if (sock == nullptr) {
    return;
  }
  SocketHandle fd = sock->GetFd();
  Connection *conn = nullptr;
  {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
      return;
    }
    conn = it->second;
    connections_.erase(it);
  }

  EventLoop *loop = conn->GetLoop();
  if (loop != nullptr) {
    loop->AddTask([conn]() { delete conn; });
  } else {
    delete conn;
  }
}

void Server::SetMessageCallback(ConnectionCallback fn) {
  message_callback_ = std::move(fn);
}

void Server::SetOnNewConnection(ConnectionCallback fn) {
  new_connection_callback_ = std::move(fn);
}
