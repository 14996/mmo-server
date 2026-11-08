/**
 * @file ServerConfig.h
 * @brief 服务端运行时配置：从 config/server.conf 加载，命令行参数可覆盖
 */
#pragma once

#include <cstdint>
#include <string>

struct ServerConfig {
  std::string ip{"0.0.0.0"};
  uint16_t port{9000};

  // 网络层
  int64_t heartbeat_timeout_ms{30000};
  uint32_t max_packets_per_sec{300};

  // 游戏世界
  int64_t save_interval_ms{30000};
  int64_t offline_retain_ms{60000};
  int aoi_cell_size{8};
};

/// 加载 key=value 配置文件；allow_missing 为 true 时文件不存在不视为错误。
/// 任何格式或取值错误都会写入 error 并返回 false。
bool LoadServerConfig(const std::string &path, ServerConfig &config,
                      std::string &error, bool allow_missing);
