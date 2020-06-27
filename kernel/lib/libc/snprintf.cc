// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdarg.h>
#include <stdio.h>

#include <ktl/algorithm.h>

namespace {

class String {
 public:
  String() = delete;
  String(char* buf, size_t size) : buffer_(buf), capacity_(size) {}

  FILE* file() { return &file_; }

  ~String() {
    if (capacity_ > 0) {
      buffer_[pos_] = '\0';
    }
  }

 private:
  FILE file_{&Callback, this};
  char* buffer_;
  size_t capacity_;
  size_t pos_ = 0;

  int Write(ktl::string_view str) {
    // The capacity includes the NUL terminator not written here.
    if (pos_ + 1 < capacity_) {
      size_t copy = ktl::min(str.size(), capacity_ - 1 - pos_);
      memcpy(&buffer_[pos_], str.data(), copy);
      pos_ += copy;
    }
    return static_cast<int>(str.size());
  }

  static int Callback(void* ptr, ktl::string_view str) {
    return static_cast<String*>(ptr)->Write(str);
  }
};

}  // namespace

int vsnprintf(char* buf, size_t len, const char* fmt, va_list args) {
  String out{buf, len};
  return vfprintf(out.file(), fmt, args);
}

int snprintf(char* buf, size_t len, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int result = vsnprintf(buf, len, fmt, args);
  va_end(args);
  return result;
}
