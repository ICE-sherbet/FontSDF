#pragma once

namespace base::serializer {

template <typename T, typename Access>
class has_member_reflect {
 private:
  template <typename U>
  static auto test(int)
      -> decltype(std::declval<U>().reflect(std::declval<Access&>()),
                  std::true_type{});
  template <typename>
  static std::false_type test(...);

 public:
  static constexpr bool value = decltype(test<T>(0))::value;
};

template <typename T, typename Access>
class has_free_reflect {
 private:
  template <typename U>
  static auto test(int)
      -> decltype(reflect(std::declval<U&>(), std::declval<Access&>()),
                  std::true_type{});
  template <typename>
  static std::false_type test(...);

 public:
  static constexpr bool value = decltype(test<T>(0))::value;
};

template <typename T, typename Access>
class has_reflect {
 private:
	template <typename U>
	static auto test(int)
			-> decltype(has_member_reflect<U, Access>::value ||
        									has_free_reflect<U, Access>::value,
        									std::true_type{});
	template <typename>
	static std::false_type test(...);

 public:
	static constexpr bool value = decltype(test<T>(0))::value;
};

template <typename T, typename Access>
class has_free_serialize {
 private:
  template <typename U>
  static auto test(int)
      -> decltype(serialize(std::declval<U&>(), std::declval<Access&>()),
                  std::true_type{});
  template <typename>
  static std::false_type test(...);

 public:
  static constexpr bool value = decltype(test<T>(0))::value;
};

template <typename T, typename Access>
class has_free_deserialize {
 private:
  template <typename U>
  static auto test(int)
      -> decltype(deserialize(std::declval<U&>(), std::declval<Access&>()),
                  std::true_type{});
  template <typename>
  static std::false_type test(...);

 public:
  static constexpr bool value = decltype(test<T>(0))::value;
};

template <typename T, typename Access>
class has_member_serialize {
 private:
  template <typename U>
  static auto test(int)
      -> decltype(std::declval<U>().serialize(std::declval<Access&>()),
                  std::true_type{});
  template <typename>
  static std::false_type test(...);

 public:
  static constexpr bool value = decltype(test<T>(0))::value;
};

template <typename T, typename Access>
class has_member_deserialize {
 private:
  template <typename U>
  static auto test(int)
      -> decltype(std::declval<U>().deserialize(std::declval<Access&>()),
                  std::true_type{});
  template <typename>
  static std::false_type test(...);

 public:
  static constexpr bool value = decltype(test<T>(0))::value;
};

template <typename T, typename Access>
class has_serialize {
 private:
	template <typename U>
	static auto test(int)
			-> decltype(has_member_serialize<U, Access>::value ||
        									has_free_serialize<U, Access>::value,
        									std::true_type{});
	template <typename>
	static std::false_type test(...);

 public:
	static constexpr bool value = decltype(test<T>(0))::value;
};

template <typename T, typename Access>
class has_deserialize {
 private:
	template <typename U>
	static auto test(int)
			-> decltype(has_member_deserialize<U, Access>::value ||
        													has_free_deserialize<U, Access>::value,
        													std::true_type{});
	template <typename>
	static std::false_type test(...);

 public:
	static constexpr bool value = decltype(test<T>(0))::value;
};

template <class T,class Impl>
concept has_write_access = requires(Impl& impl, const T& v) {
  impl.access("", v);
};

template <typename T, class Impl>
concept has_read_access =
    requires(Impl& impl, T& v) { impl.access("", v); };

template <typename T>
struct force_reflect {
  static constexpr bool value = false;
};

template <typename T>
constexpr bool is_safe_binary_pod_v =
    std::is_trivially_copyable_v<T> && !std::is_pointer_v<T> &&
    !std::is_array_v<T>;

}