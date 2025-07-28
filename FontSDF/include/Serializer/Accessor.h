#pragma once
#include "Types.h"

namespace base::serializer {

template <typename Impl>
struct Accessor {
  using ImplType = Impl;

  Impl& impl;

  template <typename T>
  inline void field(const char* key, T& value) {
    Serialize(impl, value, key);
  }

  inline void field_bytes(const void* data, size_t size)
    requires(ImplType::Direction == Direction::Serialize)
  {
    impl.write_bytes(data, size);
  }

  inline void field_bytes(void* data, size_t size)
    requires(ImplType::Direction == Direction::Deserialize)
  {
    impl.read_bytes(data, size);
  }
};
}  // namespace base::serializer