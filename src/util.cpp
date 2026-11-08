/**
 * @file util.cpp
 */
#include "util.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>

#include "Platform.h"

namespace {

std::mutex g_log_mutex;

const char *LevelName(int level) {
  switch (level) {
    case 0:
      return "INFO";
    case 1:
      return "WARN";
    case 2:
      return "ERROR";
    default:
      return "INFO";
  }
}

void LogV(int level, const char *fmt, va_list args) {
  char message[2048];
  std::vsnprintf(message, sizeof(message), fmt, args);

  int64_t ms = NowMs();
  std::time_t seconds = static_cast<std::time_t>(ms / 1000);
  int millis = static_cast<int>(ms % 1000);
  std::tm tm_value{};
#ifdef _WIN32
  localtime_s(&tm_value, &seconds);
#else
  localtime_r(&seconds, &tm_value);
#endif

  char stamp[32];
  std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &tm_value);

  std::lock_guard<std::mutex> lock(g_log_mutex);
  std::fprintf(stderr, "[%s.%03d] [%s] %s\n", stamp, millis, LevelName(level), message);
  std::fflush(stderr);
}

}  // namespace

void ErrorIf(bool condition, const char *msg) {
  if (!condition) {
    return;
  }
  std::fprintf(stderr, "[FATAL] %s (socket error=%d)\n", msg, LastSocketError());
  std::exit(EXIT_FAILURE);
}

void LogInfo(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  LogV(0, fmt, args);
  va_end(args);
}

void LogWarn(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  LogV(1, fmt, args);
  va_end(args);
}

void LogError(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  LogV(2, fmt, args);
  va_end(args);
}

int64_t NowMs() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

uint64_t Fnv1a64(const std::string &data) {
  uint64_t hash = 1469598103934665603ULL;
  for (unsigned char ch : data) {
    hash ^= ch;
    hash *= 1099511628211ULL;
  }
  return hash;
}
