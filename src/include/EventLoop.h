/**
 * @file EventLoop.h
 * @brief Reactor 事件循环：IO 事件分发 + 跨线程任务投递
 */
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <vector>

#include "Macros.h"
#include "Platform.h"

class Poller;
class Channel;

class EventLoop {
 public:
  EventLoop();
  ~EventLoop();

  DISALLOW_COPY_AND_MOVE(EventLoop);

  void Loop();
  void Quit();

  void UpdateChannel(Channel *channel);
  void DeleteChannel(Channel *channel);

  /// 线程安全：把任务投递到本 EventLoop 所在线程执行
  void AddTask(std::function<void()> task);

  bool IsInLoopThread() const;

 private:
  void HandleWakeup();
  void RunTasks();

  Poller *poller_{nullptr};
  std::atomic<bool> quit_{false};

#ifndef _WIN32
  int wakeup_fd_{-1};
  Channel *wakeup_channel_{nullptr};
#endif

  std::mutex task_mutex_;
  std::vector<std::function<void()>> tasks_;
};
