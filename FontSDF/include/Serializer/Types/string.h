#pragma once

namespace base::serializer {
template <typename A>
void serialize(std::string& str, A& a) {
  uint32_t len = static_cast<uint32_t>(str.size());
  a.field("Lenght", len);

  if constexpr (A::ImplType::IsBinary) {
    a.field_bytes(str.data(), len);
  } else {
    a.field("Text", str);
  }
};

template <typename A>
void deserialize(std::string& str, A& a) {
  uint32_t len = 0;
  a.field("Lenght", len);
  str.resize(len);
  if constexpr (A::ImplType::IsBinary) {
    a.field_bytes(str.data(), len);
  } else {
    a.field("Text", str);
  }
};

}  // namespace base::serializer