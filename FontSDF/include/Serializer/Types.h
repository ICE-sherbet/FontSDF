#pragma once

namespace base::serializer {

enum class Direction {
  Serialize,
  Deserialize,
};

enum class IsValueBinary {
	False,
  True,
};

template <Direction d, IsValueBinary is_binary>
struct BaseSerializer {
  static constexpr Direction Direction = d;
  static constexpr bool IsBinary = is_binary == IsValueBinary::True;
};
}