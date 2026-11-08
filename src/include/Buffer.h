/**
 * @file Buffer.h
 * @brief 应用层字节缓冲区（基于 std::string，简化实现）
 */
#pragma once

#include <cstddef>
#include <string>

#include "Macros.h"

class Buffer {
 public:
  Buffer() = default;
  ~Buffer() = default;

  DISALLOW_COPY_AND_MOVE(Buffer);

  void Append(const char *data, size_t size);
  void Append(const std::string &data);
  size_t Size() const;
  const char *Data() const;
  std::string ToString() const;
  void Clear();
  /// 删除前 n 个字节（非阻塞写只写出一部分时使用）
  void Erase(size_t n);

 private:
  std::string buf_;
};
