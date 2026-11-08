/**
 * @file EventLoop.cpp
 */
#include "EventLoop.h"

#include <utility>

#include "Channel.h"
#include "Poller.h"
#include "util.h"

#ifndef _WIN32
#include <sys/eventfd.h>
#include <unistd.h>
#endif

EventLoop::EventLoop() {
  poller_ = new Poller();
#ifndef _WIN32
  wakeup_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  ErrorIf(wakeup_fd_ == -1, "eventfd create error");
  wakeup_channel_ = new Channel(this, wakeup_fd_);
  wakeup_channel_->SetReadCallback([this]() { HandleWakeup(); });
  wakeup_channel_->EnableRead();
#endif
}

EventLoop::~EventLoop() {
#ifndef _WIN32
  delete wakeup_channel_;
  wakeup_channel_ = nullptr;
  if (wakeup_fd_ != -1) {
    close(wakeup_fd_);
    wakeup_fd_ = -1;
  }
#endif
  delete poller_;
  poller_ = nullptr;
}

void EventLoop::Loop() {
  quit_.store(false);
  while (!quit_.load()) {
#ifdef _WIN32
    // Windows 开发环境没有 eventfd：用短超时轮询来兼顾任务投递延迟
    const int timeout_ms = 10;
#else
    const int timeout_ms = -1;
#endif
    std::vector<Channel *> channels = poller_->Poll(timeout_ms);
    for (auto *channel : channels) {
      channel->HandleEvent();
    }
    RunTasks();
  }
  // 退出前把已投递任务跑完，保证连接析构等收尾动作执行
  RunTasks();
}

void EventLoop::Quit() {
  if (quit_.exchange(true)) {
    return;
  }
  AddTask([] {});
}

void EventLoop::UpdateChannel(Channel *channel) { poller_->UpdateChannel(channel); }

void EventLoop::DeleteChannel(Channel *channel) { poller_->DeleteChannel(channel); }

void EventLoop::AddTask(std::function<void()> task) {
  {
    std::lock_guard<std::mutex> lock(task_mutex_);
    tasks_.push_back(std::move(task));
  }
#ifndef _WIN32
  uint64_t one = 1;
  ssize_t n = write(wakeup_fd_, &one, sizeof(one));
  (void)n;
#endif
}

bool EventLoop::IsInLoopThread() const {
  // EventLoop 不持有线程对象，这里仅用于调试；实际跨线程安全由任务队列保证
  return true;
}

void EventLoop::HandleWakeup() {
#ifndef _WIN32
  uint64_t value = 0;
  while (read(wakeup_fd_, &value, sizeof(value)) > 0) {
  }
#endif
}

void EventLoop::RunTasks() {
  std::vector<std::function<void()>> tasks;
  {
    std::lock_guard<std::mutex> lock(task_mutex_);
    tasks.swap(tasks_);
  }
  for (auto &task : tasks) {
    if (task) {
      task();
    }
  }
}
