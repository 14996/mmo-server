/**
 * @file RateLimiter.h
 * @brief 单连接固定窗口限流器（线程安全性由调用方保证）
 */
#pragma once

#include <cstdint>

class RateLimiter {
 public:
  explicit RateLimiter(uint32_t limit_per_second = 300, int64_t window_ms = 1000)
      : limit_(limit_per_second), window_ms_(window_ms) {}

  /// 允许则计数并返回 true；窗口内超过限制返回 false
  bool Allow(int64_t now_ms) {
    if (now_ms - window_start_ms_ >= window_ms_) {
      window_start_ms_ = now_ms;
      count_ = 0;
    }
    if (count_ >= limit_) {
      return false;
    }
    ++count_;
    return true;
  }

  uint32_t Count() const { return count_; }
  uint32_t Limit() const { return limit_; }

 private:
  uint32_t limit_{300};
  int64_t window_ms_{1000};
  int64_t window_start_ms_{0};
  uint32_t count_{0};
};
