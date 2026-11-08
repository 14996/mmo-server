/**
 * @file Buffer.h
 * @brief 环形字节缓冲区：读写游标分离，避免每次消费都搬移剩余数据
 *
 * 读取时通过 ReadPtr()/ReadableBytes() 取当前连续可读段；跨过环形边界时
 * 调用方循环 Retrieve() 即可，无需把整个缓冲区压平。
 */
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Macros.h"

class Buffer {
 public:
  Buffer() = default;
  ~Buffer() = default;

  DISALLOW_COPY_AND_MOVE(Buffer);

  void Append(const char *data, size_t size);
  void Append(const std::string &data);

  /// 把全部可读数据追加到 out，不消费缓冲区
  void AppendTo(std::string &out) const;

  size_t Size() const { return size_; }

  /// 从 ReadPtr() 开始连续可读的字节数（可能小于 Size）
  size_t ReadableBytes() const;

  /// 当前连续可读段起始地址；Size()==0 时调用无意义
  const char *ReadPtr() const;

  std::string ToString() const;

  /// 消费前 n 个可读字节
  void Retrieve(size_t n);

  /// 清空数据，但保留已分配的容量供复用
  void Clear();

 private:
  void EnsureWritable(size_t n);
  void CopyReadable(char *out) const;

  std::vector<char> data_;
  size_t read_pos_{0};
  size_t write_pos_{0};
  size_t size_{0};
};
