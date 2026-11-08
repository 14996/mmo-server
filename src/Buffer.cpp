/**
 * @file Buffer.cpp
 */
#include "Buffer.h"

void Buffer::Append(const char *data, size_t size) {
  if (data == nullptr || size == 0) {
    return;
  }
  buf_.append(data, size);
}

void Buffer::Append(const std::string &data) { buf_.append(data); }

size_t Buffer::Size() const { return buf_.size(); }

const char *Buffer::Data() const { return buf_.data(); }

std::string Buffer::ToString() const { return buf_; }

void Buffer::Clear() { buf_.clear(); }

void Buffer::Erase(size_t n) {
  if (n >= buf_.size()) {
    buf_.clear();
  } else {
    buf_.erase(0, n);
  }
}
