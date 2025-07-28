#pragma once

namespace base::serializer {

template <typename A>
void serialize(const char*& str, A& a) {
  uint32_t len = static_cast<uint32_t>(std::strlen(str));
  a.field("Lenght", len);
  a.field("Text", str);
}

template <typename A>
void deserialize(const char*& str, A& a) {
  uint32_t len = 0;
  a.field("Lenght", len);
  char* tmp = new char[len + 1];
  a.field_bytes(tmp, len);
  tmp[len] = '\0';
  str = tmp;
}

template <typename T, size_t N, typename A>
void serialize(T (&arr)[N], A& a) {
  if constexpr (A::ImplType::IsBinary && is_safe_binary_pod_v<T>) {
    a.field_bytes(arr, N * sizeof(T));
  } else {
    for (size_t i = 0; i < N; ++i) a.field(nullptr, arr[i]);
  }
}

template <typename T, size_t N, typename A>
void deserialize(T (&arr)[N], A& a) {
  if constexpr (A::ImplType::IsBinary && is_safe_binary_pod_v<T>) {
    a.field_bytes(arr, N * sizeof(T));
  } else {
    for (size_t i = 0; i < N; ++i) a.field(nullptr, arr[i]);
  }
}

}