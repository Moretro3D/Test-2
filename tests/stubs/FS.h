#pragma once
#include <cstddef>
#include <cstdint>

#define FILE_READ "r"
#define FILE_WRITE "w"

class File {
public:
  explicit operator bool() const { return true; }
  size_t size() const { return 0; }
  size_t read(uint8_t *, size_t) { return 0; }
  size_t write(const uint8_t *, size_t len) { return len; }
  void flush() {}
  void close() {}
};
