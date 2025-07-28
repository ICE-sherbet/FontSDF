#pragma once

namespace base::serializer {
template <typename T, typename A>
void serialize(std::vector<T>& vec, A& a) {
  uint32_t len = static_cast<uint32_t>(vec.size());
  a.field("Lenght", len);

  if constexpr (A::ImplType::IsBinary && is_safe_binary_pod_v<T>) {
    if (len > 0) a.field_bytes(vec.data(), len * sizeof(T));
  } else {
    for (uint32_t i = 0; i < len; ++i) a.field(nullptr, vec[i]);
  }
};

template <typename T, typename A>
void deserialize(std::vector<T>& vec, A& a) {
  uint32_t len = 0;
  a.field("Lenght", len);
  vec.resize(len);

  if constexpr (A::ImplType::IsBinary && is_safe_binary_pod_v<T>) {
    if (len > 0) a.field_bytes(vec.data(), len * sizeof(T));
  } else {
    for (uint32_t i = 0; i < len; ++i) a.field(nullptr, vec[i]);
  }
};

}  // namespace base::serializer