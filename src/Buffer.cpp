/**
 * @file Buffer.cpp
 */
#include "Buffer.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {

constexpr size_t kInitialCapacity = 1024;

}  // namespace

void Buffer::Append(const char *data, size_t size) {
  if (data == nullptr || size == 0) {
    return;
  }
  if (size > std::numeric_limits<size_t>::max() - size_) {
    throw std::length_error("buffer append overflow");
  }

  EnsureWritable(size);

  const size_t capacity = data_.size();
  const size_t first = std::min(size, capacity - write_pos_);
  std::memcpy(data_.data() + write_pos_, data, first);
  if (size > first) {
    std::memcpy(data_.data(), data + first, size - first);
  }
  write_pos_ = (write_pos_ + size) % capacity;
  size_ += size;
}

void Buffer::Append(const std::string &data) { Append(data.data(), data.size()); }

void Buffer::AppendTo(std::string &out) const {
  if (size_ == 0) {
    return;
  }
  const size_t first = ReadableBytes();
  out.append(data_.data() + read_pos_, first);
  if (size_ > first) {
    out.append(data_.data(), size_ - first);
  }
}

size_t Buffer::ReadableBytes() const {
  if (size_ == 0 || data_.empty()) {
    return 0;
  }
  return std::min(size_, data_.size() - read_pos_);
}

const char *Buffer::ReadPtr() const {
  if (data_.empty()) {
    return "";
  }
  return data_.data() + read_pos_;
}

std::string Buffer::ToString() const {
  std::string out;
  out.reserve(size_);
  AppendTo(out);
  return out;
}

void Buffer::Retrieve(size_t n) {
  if (n == 0) {
    return;
  }
  if (n >= size_) {
    Clear();
    return;
  }
  read_pos_ = (read_pos_ + n) % data_.size();
  size_ -= n;
}

void Buffer::Clear() {
  read_pos_ = 0;
  write_pos_ = 0;
  size_ = 0;
}

void Buffer::EnsureWritable(size_t n) {
  const size_t required = size_ + n;
  if (required <= data_.size()) {
    return;  // 环形空间足够，Append 会处理回绕
  }

  size_t new_capacity = data_.empty() ? kInitialCapacity : data_.size();
  while (new_capacity < required) {
    if (new_capacity > std::numeric_limits<size_t>::max() / 2) {
      new_capacity = required;
      break;
    }
    new_capacity *= 2;
  }

  std::vector<char> next(new_capacity);
  CopyReadable(next.data());
  data_.swap(next);
  read_pos_ = 0;
  write_pos_ = size_;
}

void Buffer::CopyReadable(char *out) const {
  if (size_ == 0) {
    return;
  }
  const size_t first = ReadableBytes();
  std::memcpy(out, data_.data() + read_pos_, first);
  if (size_ > first) {
    std::memcpy(out + first, data_.data(), size_ - first);
  }
}
