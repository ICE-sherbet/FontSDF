#pragma once
#include <cstdint>
#include <string>

#include "Traits.h"
#include "Types.h"

namespace base::serializer {

struct BinaryWriter : public BaseSerializer<Direction::Serialize, IsValueBinary::True> {
  uint8_t* buffer;
  size_t pos;

  BinaryWriter(uint8_t* buf) : buffer(buf), pos(0) {}

  template <typename T>
    requires(is_safe_binary_pod_v<T>)
  void access(const char* /*key*/, const T& value) {
    std::memcpy(buffer + pos, &value, sizeof(T));
    pos += sizeof(T);
  }

  void write_bytes(const void* data, size_t size) {
    std::memcpy(buffer + pos, data, size);
    pos += size;
  }
};

struct BinaryReader
    : public BaseSerializer<Direction::Deserialize, IsValueBinary::True> {
  const uint8_t* buffer;
  size_t pos;

  BinaryReader(const uint8_t* buf) : buffer(buf), pos(0) {}

  template <typename T>
  requires(is_safe_binary_pod_v<T>)
  void access(const char* /*key*/, T& value) {
    std::memcpy(&value, buffer + pos, sizeof(T));
    pos += sizeof(T);
  }

  void read_bytes(void* data, size_t size) {
    std::memcpy(data, buffer + pos, size);
    pos += size;
  }
};

}  // namespace base::serializer