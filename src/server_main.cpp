/**
 * @file server_main.cpp
 * @brief MMO 服务器入口：参数解析、信号处理、主从 reactor 启动与优雅退出
 */
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <thread>

#include "EventLoop.h"
#include "GameServer.h"
#include "Platform.h"
#include "Server.h"
#include "util.h"

namespace {

volatile std::sig_atomic_t g_stop = 0;

void HandleSignal(int /*sig*/) { g_stop = 1; }

#ifdef _WIN32
BOOL WINAPI ConsoleHandler(DWORD event_type) {
  if (event_type == CTRL_C_EVENT || event_type == CTRL_BREAK_EVENT ||
      event_type == CTRL_CLOSE_EVENT) {
    g_stop = 1;
    return TRUE;
  }
  return FALSE;
}
#endif

void PrintUsage(const char *program) {
  std::printf(
      "Usage: %s [--ip 0.0.0.0] [--port 9000] [--run-seconds 0]\n"
      "  --run-seconds N  auto shutdown after N seconds (0 = run forever), "
      "for smoke tests\n",
      program);
}

}  // namespace

int main(int argc, char **argv) {
  uint16_t port = 9000;
  std::string ip = "0.0.0.0";
  int run_seconds = 0;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--port" && i + 1 < argc) {
      int value = std::atoi(argv[++i]);
      if (value <= 0 || value > 65535) {
        std::fprintf(stderr, "invalid port: %s\n", argv[i]);
        return 1;
      }
      port = static_cast<uint16_t>(value);
    } else if (arg == "--ip" && i + 1 < argc) {
      ip = argv[++i];
    } else if (arg == "--run-seconds" && i + 1 < argc) {
      run_seconds = std::atoi(argv[++i]);
      if (run_seconds < 0) {
        std::fprintf(stderr, "invalid run-seconds: %s\n", argv[i]);
        return 1;
      }
    } else if (arg == "--help" || arg == "-h") {
      PrintUsage(argv[0]);
      return 0;
    } else {
      std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
      PrintUsage(argv[0]);
      return 1;
    }
  }

  IgnoreSigPipe();
  if (!SocketInit()) {
    std::fprintf(stderr, "socket init failed: %d\n", LastSocketError());
    return 1;
  }

#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCtrlHandler(ConsoleHandler, TRUE);
#endif
  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);

  int exit_code = 0;
  try {
    EventLoop main_loop;
    auto server = std::make_unique<Server>(&main_loop, ip.c_str(), port);
    auto game = std::make_unique<GameServer>(server.get());
    game->Start();

    LogInfo("mmo server is ready: %s:%u (Ctrl+C to stop)", ip.c_str(),
            static_cast<unsigned>(port));

    const int64_t shutdown_at =
        run_seconds > 0 ? NowMs() + static_cast<int64_t>(run_seconds) * 1000 : 0;

    std::thread loop_thread([&main_loop]() { main_loop.Loop(); });
    while (g_stop == 0 && (shutdown_at == 0 || NowMs() < shutdown_at)) {
      SleepMs(100);
    }

    LogInfo("shutdown requested (signal=%d, run_seconds=%d), stopping...",
            static_cast<int>(g_stop), run_seconds);
    main_loop.Quit();
    loop_thread.join();

    // 先停业务线程并落盘，再销毁 Server（会关闭 sub reactor 与所有连接）
    game->Stop();
    server.reset();
    game.reset();
  } catch (const std::exception &ex) {
    LogError("fatal: %s", ex.what());
    exit_code = 1;
  }

  SocketCleanup();
  LogInfo("server exited");
  return exit_code;
}
