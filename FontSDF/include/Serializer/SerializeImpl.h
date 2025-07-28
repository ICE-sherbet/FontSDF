#pragma once
#include "Types.h"
#include "Accessor.h"

namespace base::serializer {

template <typename Impl, typename T>
inline void Serialize(Impl& impl, T& obj, const char* key = nullptr) {
  serialize_or_deserialize_impl<Impl::Direction>(impl, obj, key);
}


template <Direction Dir, typename Impl, typename T>
inline void serialize_or_deserialize_impl(Impl& impl, T& obj,
                                          const char* key = nullptr) {
  if constexpr (has_write_access<T, Impl> || has_read_access<T, Impl> ||
                (!force_reflect<T>::value && is_safe_binary_pod_v<T>)) {
    impl.access(key, obj);
  } else if constexpr (Dir == Direction::Serialize &&
                       has_member_serialize<T, Impl>::value) {
    Accessor<Impl> a{impl};
    //obj.serialize(a);
  } else if constexpr (Dir == Direction::Deserialize &&
                       has_member_deserialize<T, Impl>::value) {
    Accessor<Impl> a{impl};
    obj.deserialize(a);
  } else if constexpr (Dir == Direction::Serialize &&
                       has_free_serialize<T, Impl>::value) {
    Accessor<Impl> a{impl};
    serialize(obj, a);
  } else if constexpr (Dir == Direction::Deserialize &&
                       has_free_deserialize<T, Impl>::value) {
    Accessor<Impl> a{impl};
    deserialize(obj, a);
  } else if constexpr (has_member_reflect<T, Impl>::value) {
    Accessor<Impl> a{impl};
    obj.reflect(a);  // 共通ケース
  } else if constexpr (has_free_reflect<T, Impl>::value) {
    Accessor<Impl> a{impl};
    reflect(obj, a);  // 共通ケース
  } else {
    static_assert(!std::is_same_v<T, T>,
                  "No suitable serialization method found for type");
  }
}
}