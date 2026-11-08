/**
 * @file util.h
 * @brief 日志、时间、哈希等通用工具
 */
#pragma once

#include <cstdint>
#include <string>

/// 出错时打印信息并退出，沿用原框架语义
void ErrorIf(bool condition, const char *msg);

void LogInfo(const char *fmt, ...);
void LogWarn(const char *fmt, ...);
void LogError(const char *fmt, ...);

/// 单调时钟毫秒数（用于冷却、心跳、Tick 计时）
int64_t NowMs();

/// 稳定的 64 位 FNV-1a 哈希（跨平台一致，用于密码摘要存盘）
uint64_t Fnv1a64(const std::string &data);
