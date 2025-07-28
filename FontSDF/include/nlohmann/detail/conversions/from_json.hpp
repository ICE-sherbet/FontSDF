//     __ _____ _____ _____
//  __|  |   __|     |   | |  JSON for Modern C++
// |  |  |__   |  |  | | | |  version 3.12.0
// |_____|_____|_____|_|___|  https://github.com/nlohmann/json
//
// SPDX-FileCopyrightText: 2013 - 2025 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm> // transform
#include <array> // array
#include <forward_list> // forward_list
#include <iterator> // inserter, front_inserter, end
#include <map> // map
#include <string> // string
#include <tuple> // tuple, make_tuple
#include <type_traits> // is_arithmetic, is_same, is_enum, underlying_type, is_convertible
#include <unordered_map> // unordered_map
#include <utility> // pair, declval
#include <valarray> // valarray

#include <nlohmann/detail/exceptions.hpp>
#include <nlohmann/detail/macro_scope.hpp>
#include <nlohmann/detail/meta/cpp_future.hpp>
#include <nlohmann/detail/meta/identity_tag.hpp>
#include <nlohmann/detail/meta/std_fs.hpp>
#include <nlohmann/detail/meta/type_traits.hpp>
#include <nlohmann/detail/string_concat.hpp>
#include <nlohmann/detail/value_t.hpp>

// include after macro_scope.hpp
#ifdef JSON_HAS_CPP_17
    #include <optional> // optional
#endif

#if JSON_HAS_FILESYSTEM || JSON_HAS_EXPERIMENTAL_FILESYSTEM
    #include <string_view> // u8string_view
#endif

NLOHMANN_JSON_NAMESPACE_BEGIN
namespace detail
{

template<typename BasicJsonType>
inline void from_json(const BasicJsonType& out, typename std::nullptr_t& n)
{
    if (JSON_HEDLEY_UNLIKELY(!out.is_null()))
    {
        JSON_THROW(type_error::create(302, concat("type must be null, but is ", out.type_name()), &out));
    }
    n = nullptr;
}

#ifdef JSON_HAS_CPP_17
template<typename BasicJsonType, typename T>
void from_json(const BasicJsonType& out, std::optional<T>& opt)
{
    if (out.is_null())
    {
        opt = std::nullopt;
    }
    else
    {
        opt.emplace(out.template get<T>());
    }
}
#endif // JSON_HAS_CPP_17

// overloads for basic_json template parameters
template < typename BasicJsonType, typename ArithmeticType,
           enable_if_t < std::is_arithmetic<ArithmeticType>::value&&
                         !std::is_same<ArithmeticType, typename BasicJsonType::boolean_t>::value,
                         int > = 0 >
void get_arithmetic_value(const BasicJsonType& out, ArithmeticType& val)
{
    switch (static_cast<value_t>(out))
    {
        case value_t::number_unsigned:
        {
            val = static_cast<ArithmeticType>(*out.template get_ptr<const typename BasicJsonType::number_unsigned_t*>());
            break;
        }
        case value_t::number_integer:
        {
            val = static_cast<ArithmeticType>(*out.template get_ptr<const typename BasicJsonType::number_integer_t*>());
            break;
        }
        case value_t::number_float:
        {
            val = static_cast<ArithmeticType>(*out.template get_ptr<const typename BasicJsonType::number_float_t*>());
            break;
        }

        case value_t::null:
        case value_t::object:
        case value_t::array:
        case value_t::string:
        case value_t::boolean:
        case value_t::binary:
        case value_t::discarded:
        default:
            JSON_THROW(type_error::create(302, concat("type must be number, but is ", out.type_name()), &out));
    }
}

template<typename BasicJsonType>
inline void from_json(const BasicJsonType& out, typename BasicJsonType::boolean_t& b)
{
    if (JSON_HEDLEY_UNLIKELY(!out.is_boolean()))
    {
        JSON_THROW(type_error::create(302, concat("type must be boolean, but is ", out.type_name()), &out));
    }
    b = *out.template get_ptr<const typename BasicJsonType::boolean_t*>();
}

template<typename BasicJsonType>
inline void from_json(const BasicJsonType& out, typename BasicJsonType::string_t& s)
{
    if (JSON_HEDLEY_UNLIKELY(!out.is_string()))
    {
        JSON_THROW(type_error::create(302, concat("type must be string, but is ", out.type_name()), &out));
    }
    s = *out.template get_ptr<const typename BasicJsonType::string_t*>();
}

template <
    typename BasicJsonType, typename StringType,
    enable_if_t <
        std::is_assignable<StringType&, const typename BasicJsonType::string_t>::value
        && is_detected_exact<typename BasicJsonType::string_t::value_type, value_type_t, StringType>::value
        && !std::is_same<typename BasicJsonType::string_t, StringType>::value
        && !is_json_ref<StringType>::value, int > = 0 >
inline void from_json(const BasicJsonType& out, StringType& s)
{
    if (JSON_HEDLEY_UNLIKELY(!out.is_string()))
    {
        JSON_THROW(type_error::create(302, concat("type must be string, but is ", out.type_name()), &out));
    }

    s = *out.template get_ptr<const typename BasicJsonType::string_t*>();
}

template<typename BasicJsonType>
inline void from_json(const BasicJsonType& out, typename BasicJsonType::number_float_t& val)
{
    get_arithmetic_value(out, val);
}

template<typename BasicJsonType>
inline void from_json(const BasicJsonType& out, typename BasicJsonType::number_unsigned_t& val)
{
    get_arithmetic_value(out, val);
}

template<typename BasicJsonType>
inline void from_json(const BasicJsonType& out, typename BasicJsonType::number_integer_t& val)
{
    get_arithmetic_value(out, val);
}

#if !JSON_DISABLE_ENUM_SERIALIZATION
template<typename BasicJsonType, typename EnumType,
         enable_if_t<std::is_enum<EnumType>::value, int> = 0>
inline void from_json(const BasicJsonType& out, EnumType& e)
{
    typename std::underlying_type<EnumType>::type val;
    get_arithmetic_value(out, val);
    e = static_cast<EnumType>(val);
}
#endif  // JSON_DISABLE_ENUM_SERIALIZATION

// forward_list doesn't have an insert method
template<typename BasicJsonType, typename T, typename Allocator,
         enable_if_t<is_getable<BasicJsonType, T>::value, int> = 0>
inline void from_json(const BasicJsonType& out, std::forward_list<T, Allocator>& l)
{
    if (JSON_HEDLEY_UNLIKELY(!out.is_array()))
    {
        JSON_THROW(type_error::create(302, concat("type must be array, but is ", out.type_name()), &out));
    }
    l.clear();
    std::transform(out.rbegin(), out.rend(),
                   std::front_inserter(l), [](const BasicJsonType & i)
    {
        return i.template get<T>();
    });
}

// valarray doesn't have an insert method
template<typename BasicJsonType, typename T,
         enable_if_t<is_getable<BasicJsonType, T>::value, int> = 0>
inline void from_json(const BasicJsonType& out, std::valarray<T>& l)
{
    if (JSON_HEDLEY_UNLIKELY(!out.is_array()))
    {
        JSON_THROW(type_error::create(302, concat("type must be array, but is ", out.type_name()), &out));
    }
    l.resize(out.size());
    std::transform(out.begin(), out.end(), std::begin(l),
                   [](const BasicJsonType & elem)
    {
        return elem.template get<T>();
    });
}

template<typename BasicJsonType, typename T, std::size_t N>
auto from_json(const BasicJsonType& out, T (&arr)[N])  // NOLINT(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
-> decltype(out.template get<T>(), void())
{
    for (std::size_t i = 0; i < N; ++i)
    {
        arr[i] = out.at(i).template get<T>();
    }
}

template<typename BasicJsonType, typename T, std::size_t N1, std::size_t N2>
auto from_json(const BasicJsonType& out, T (&arr)[N1][N2])  // NOLINT(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
-> decltype(out.template get<T>(), void())
{
    for (std::size_t i1 = 0; i1 < N1; ++i1)
    {
        for (std::size_t i2 = 0; i2 < N2; ++i2)
        {
            arr[i1][i2] = out.at(i1).at(i2).template get<T>();
        }
    }
}

template<typename BasicJsonType, typename T, std::size_t N1, std::size_t N2, std::size_t N3>
auto from_json(const BasicJsonType& out, T (&arr)[N1][N2][N3])  // NOLINT(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
-> decltype(out.template get<T>(), void())
{
    for (std::size_t i1 = 0; i1 < N1; ++i1)
    {
        for (std::size_t i2 = 0; i2 < N2; ++i2)
        {
            for (std::size_t i3 = 0; i3 < N3; ++i3)
            {
                arr[i1][i2][i3] = out.at(i1).at(i2).at(i3).template get<T>();
            }
        }
    }
}

template<typename BasicJsonType, typename T, std::size_t N1, std::size_t N2, std::size_t N3, std::size_t N4>
auto from_json(const BasicJsonType& out, T (&arr)[N1][N2][N3][N4])  // NOLINT(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays)
-> decltype(out.template get<T>(), void())
{
    for (std::size_t i1 = 0; i1 < N1; ++i1)
    {
        for (std::size_t i2 = 0; i2 < N2; ++i2)
        {
            for (std::size_t i3 = 0; i3 < N3; ++i3)
            {
                for (std::size_t i4 = 0; i4 < N4; ++i4)
                {
                    arr[i1][i2][i3][i4] = out.at(i1).at(i2).at(i3).at(i4).template get<T>();
                }
            }
        }
    }
}

template<typename BasicJsonType>
inline void from_json_array_impl(const BasicJsonType& out, typename BasicJsonType::array_t& arr, priority_tag<3> /*unused*/)
{
    arr = *out.template get_ptr<const typename BasicJsonType::array_t*>();
}

template<typename BasicJsonType, typename T, std::size_t N>
auto from_json_array_impl(const BasicJsonType& out, std::array<T, N>& arr,
                          priority_tag<2> /*unused*/)
-> decltype(out.template get<T>(), void())
{
    for (std::size_t i = 0; i < N; ++i)
    {
        arr[i] = out.at(i).template get<T>();
    }
}

template<typename BasicJsonType, typename ConstructibleArrayType,
         enable_if_t<
             std::is_assignable<ConstructibleArrayType&, ConstructibleArrayType>::value,
             int> = 0>
auto from_json_array_impl(const BasicJsonType& out, ConstructibleArrayType& arr, priority_tag<1> /*unused*/)
-> decltype(
    arr.reserve(std::declval<typename ConstructibleArrayType::size_type>()),
    out.template get<typename ConstructibleArrayType::value_type>(),
    void())
{
    using std::end;

    ConstructibleArrayType ret;
    ret.reserve(out.size());
    std::transform(out.begin(), out.end(),
                   std::inserter(ret, end(ret)), [](const BasicJsonType & i)
    {
        // get<BasicJsonType>() returns *this, this won't call a from_json
        // method when value_type is BasicJsonType
        return i.template get<typename ConstructibleArrayType::value_type>();
    });
    arr = std::move(ret);
}

template<typename BasicJsonType, typename ConstructibleArrayType,
         enable_if_t<
             std::is_assignable<ConstructibleArrayType&, ConstructibleArrayType>::value,
             int> = 0>
inline void from_json_array_impl(const BasicJsonType& out, ConstructibleArrayType& arr,
                                 priority_tag<0> /*unused*/)
{
    using std::end;

    ConstructibleArrayType ret;
    std::transform(
        out.begin(), out.end(), std::inserter(ret, end(ret)),
        [](const BasicJsonType & i)
    {
        // get<BasicJsonType>() returns *this, this won't call a from_json
        // method when value_type is BasicJsonType
        return i.template get<typename ConstructibleArrayType::value_type>();
    });
    arr = std::move(ret);
}

template < typename BasicJsonType, typename ConstructibleArrayType,
           enable_if_t <
               is_constructible_array_type<BasicJsonType, ConstructibleArrayType>::value&&
               !is_constructible_object_type<BasicJsonType, ConstructibleArrayType>::value&&
               !is_constructible_string_type<BasicJsonType, ConstructibleArrayType>::value&&
               !std::is_same<ConstructibleArrayType, typename BasicJsonType::binary_t>::value&&
               !is_basic_json<ConstructibleArrayType>::value,
               int > = 0 >
auto from_json(const BasicJsonType& out, ConstructibleArrayType& arr)
-> decltype(from_json_array_impl(out, arr, priority_tag<3> {}),
out.template get<typename ConstructibleArrayType::value_type>(),
void())
{
    if (JSON_HEDLEY_UNLIKELY(!out.is_array()))
    {
        JSON_THROW(type_error::create(302, concat("type must be array, but is ", out.type_name()), &out));
    }

    from_json_array_impl(out, arr, priority_tag<3> {});
}

template < typename BasicJsonType, typename T, std::size_t... Idx >
std::array<T, sizeof...(Idx)> from_json_inplace_array_impl(BasicJsonType&& out,
                     identity_tag<std::array<T, sizeof...(Idx)>> /*unused*/, index_sequence<Idx...> /*unused*/)
{
    return { { std::forward<BasicJsonType>(out).at(Idx).template get<T>()... } };
}

template < typename BasicJsonType, typename T, std::size_t N >
auto from_json(BasicJsonType&& out, identity_tag<std::array<T, N>> tag)
-> decltype(from_json_inplace_array_impl(std::forward<BasicJsonType>(out), tag, make_index_sequence<N> {}))
{
    if (JSON_HEDLEY_UNLIKELY(!out.is_array()))
    {
        JSON_THROW(type_error::create(302, concat("type must be array, but is ", out.type_name()), &out));
    }

    return from_json_inplace_array_impl(std::forward<BasicJsonType>(out), tag, make_index_sequence<N> {});
}

template<typename BasicJsonType>
inline void from_json(const BasicJsonType& out, typename BasicJsonType::binary_t& bin)
{
    if (JSON_HEDLEY_UNLIKELY(!out.is_binary()))
    {
        JSON_THROW(type_error::create(302, concat("type must be binary, but is ", out.type_name()), &out));
    }

    bin = *out.template get_ptr<const typename BasicJsonType::binary_t*>();
}

template<typename BasicJsonType, typename ConstructibleObjectType,
         enable_if_t<is_constructible_object_type<BasicJsonType, ConstructibleObjectType>::value, int> = 0>
inline void from_json(const BasicJsonType& out, ConstructibleObjectType& obj)
{
    if (JSON_HEDLEY_UNLIKELY(!out.is_object()))
    {
        JSON_THROW(type_error::create(302, concat("type must be object, but is ", out.type_name()), &out));
    }

    ConstructibleObjectType ret;
    const auto* inner_object = out.template get_ptr<const typename BasicJsonType::object_t*>();
    using value_type = typename ConstructibleObjectType::value_type;
    std::transform(
        inner_object->begin(), inner_object->end(),
        std::inserter(ret, ret.begin()),
        [](typename BasicJsonType::object_t::value_type const & p)
    {
        return value_type(p.first, p.second.template get<typename ConstructibleObjectType::mapped_type>());
    });
    obj = std::move(ret);
}

// overload for arithmetic types, not chosen for basic_json template arguments
// (BooleanType, etc.); note: Is it really necessary to provide explicit
// overloads for boolean_t etc. in case of a custom BooleanType which is not
// an arithmetic type?
template < typename BasicJsonType, typename ArithmeticType,
           enable_if_t <
               std::is_arithmetic<ArithmeticType>::value&&
               !std::is_same<ArithmeticType, typename BasicJsonType::number_unsigned_t>::value&&
               !std::is_same<ArithmeticType, typename BasicJsonType::number_integer_t>::value&&
               !std::is_same<ArithmeticType, typename BasicJsonType::number_float_t>::value&&
               !std::is_same<ArithmeticType, typename BasicJsonType::boolean_t>::value,
               int > = 0 >
inline void from_json(const BasicJsonType& out, ArithmeticType& val)
{
    switch (static_cast<value_t>(out))
    {
        case value_t::number_unsigned:
        {
            val = static_cast<ArithmeticType>(*out.template get_ptr<const typename BasicJsonType::number_unsigned_t*>());
            break;
        }
        case value_t::number_integer:
        {
            val = static_cast<ArithmeticType>(*out.template get_ptr<const typename BasicJsonType::number_integer_t*>());
            break;
        }
        case value_t::number_float:
        {
            val = static_cast<ArithmeticType>(*out.template get_ptr<const typename BasicJsonType::number_float_t*>());
            break;
        }
        case value_t::boolean:
        {
            val = static_cast<ArithmeticType>(*out.template get_ptr<const typename BasicJsonType::boolean_t*>());
            break;
        }

        case value_t::null:
        case value_t::object:
        case value_t::array:
        case value_t::string:
        case value_t::binary:
        case value_t::discarded:
        default:
            JSON_THROW(type_error::create(302, concat("type must be number, but is ", out.type_name()), &out));
    }
}

template<typename BasicJsonType, typename... Args, std::size_t... Idx>
std::tuple<Args...> from_json_tuple_impl_base(BasicJsonType&& out, index_sequence<Idx...> /*unused*/)
{
    return std::make_tuple(std::forward<BasicJsonType>(out).at(Idx).template get<Args>()...);
}

template<typename BasicJsonType>
std::tuple<> from_json_tuple_impl_base(BasicJsonType& /*unused*/, index_sequence<> /*unused*/)
{
    return {};
}

template < typename BasicJsonType, class A1, class A2 >
std::pair<A1, A2> from_json_tuple_impl(BasicJsonType&& out, identity_tag<std::pair<A1, A2>> /*unused*/, priority_tag<0> /*unused*/)
{
    return {std::forward<BasicJsonType>(out).at(0).template get<A1>(),
            std::forward<BasicJsonType>(out).at(1).template get<A2>()};
}

template<typename BasicJsonType, typename A1, typename A2>
inline void from_json_tuple_impl(BasicJsonType&& out, std::pair<A1, A2>& p, priority_tag<1> /*unused*/)
{
    p = from_json_tuple_impl(std::forward<BasicJsonType>(out), identity_tag<std::pair<A1, A2>> {}, priority_tag<0> {});
}

template<typename BasicJsonType, typename... Args>
std::tuple<Args...> from_json_tuple_impl(BasicJsonType&& out, identity_tag<std::tuple<Args...>> /*unused*/, priority_tag<2> /*unused*/)
{
    return from_json_tuple_impl_base<BasicJsonType, Args...>(std::forward<BasicJsonType>(out), index_sequence_for<Args...> {});
}

template<typename BasicJsonType, typename... Args>
inline void from_json_tuple_impl(BasicJsonType&& out, std::tuple<Args...>& t, priority_tag<3> /*unused*/)
{
    t = from_json_tuple_impl_base<BasicJsonType, Args...>(std::forward<BasicJsonType>(out), index_sequence_for<Args...> {});
}

template<typename BasicJsonType, typename TupleRelated>
auto from_json(BasicJsonType&& out, TupleRelated&& t)
-> decltype(from_json_tuple_impl(std::forward<BasicJsonType>(out), std::forward<TupleRelated>(t), priority_tag<3> {}))
{
    if (JSON_HEDLEY_UNLIKELY(!out.is_array()))
    {
        JSON_THROW(type_error::create(302, concat("type must be array, but is ", out.type_name()), &out));
    }

    return from_json_tuple_impl(std::forward<BasicJsonType>(out), std::forward<TupleRelated>(t), priority_tag<3> {});
}

template < typename BasicJsonType, typename Key, typename Value, typename Compare, typename Allocator,
           typename = enable_if_t < !std::is_constructible <
                                        typename BasicJsonType::string_t, Key >::value >>
inline void from_json(const BasicJsonType& out, std::map<Key, Value, Compare, Allocator>& m)
{
    if (JSON_HEDLEY_UNLIKELY(!out.is_array()))
    {
        JSON_THROW(type_error::create(302, concat("type must be array, but is ", out.type_name()), &out));
    }
    m.clear();
    for (const auto& p : out)
    {
        if (JSON_HEDLEY_UNLIKELY(!p.is_array()))
        {
            JSON_THROW(type_error::create(302, concat("type must be array, but is ", p.type_name()), &out));
        }
        m.emplace(p.at(0).template get<Key>(), p.at(1).template get<Value>());
    }
}

template < typename BasicJsonType, typename Key, typename Value, typename Hash, typename KeyEqual, typename Allocator,
           typename = enable_if_t < !std::is_constructible <
                                        typename BasicJsonType::string_t, Key >::value >>
inline void from_json(const BasicJsonType& out, std::unordered_map<Key, Value, Hash, KeyEqual, Allocator>& m)
{
    if (JSON_HEDLEY_UNLIKELY(!out.is_array()))
    {
        JSON_THROW(type_error::create(302, concat("type must be array, but is ", out.type_name()), &out));
    }
    m.clear();
    for (const auto& p : out)
    {
        if (JSON_HEDLEY_UNLIKELY(!p.is_array()))
        {
            JSON_THROW(type_error::create(302, concat("type must be array, but is ", p.type_name()), &out));
        }
        m.emplace(p.at(0).template get<Key>(), p.at(1).template get<Value>());
    }
}

#if JSON_HAS_FILESYSTEM || JSON_HAS_EXPERIMENTAL_FILESYSTEM
template<typename BasicJsonType>
inline void from_json(const BasicJsonType& out, std_fs::path& p)
{
    if (JSON_HEDLEY_UNLIKELY(!out.is_string()))
    {
        JSON_THROW(type_error::create(302, concat("type must be string, but is ", out.type_name()), &out));
    }
    const auto& s = *out.template get_ptr<const typename BasicJsonType::string_t*>();
    // Checking for C++20 standard or later can be insufficient in case the
    // library support for char8_t is either incomplete or was disabled
    // altogether. Use the __cpp_lib_char8_t feature test instead.
#if defined(__cpp_lib_char8_t) && (__cpp_lib_char8_t >= 201907L)
    p = std_fs::path(std::u8string_view(reinterpret_cast<const char8_t*>(s.data()), s.size()));
#else
    p = std_fs::u8path(s); // accepts UTF-8 encoded std::string in C++17, deprecated in C++20
#endif
}
#endif

struct from_json_fn
{
    template<typename BasicJsonType, typename T>
    auto operator()(const BasicJsonType& out, T&& val) const
    noexcept(noexcept(from_json(out, std::forward<T>(val))))
    -> decltype(from_json(out, std::forward<T>(val)))
    {
        return from_json(out, std::forward<T>(val));
    }
};

}  // namespace detail

#ifndef JSON_HAS_CPP_17
/// namespace to hold default `from_json` function
/// to see why this is required:
/// http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2015/n4381.html
namespace // NOLINT(cert-dcl59-cpp,fuchsia-header-anon-namespaces,google-build-namespaces)
{
#endif
JSON_INLINE_VARIABLE constexpr const auto& from_json = // NOLINT(misc-definitions-in-headers)
    detail::static_const<detail::from_json_fn>::value;
#ifndef JSON_HAS_CPP_17
}  // namespace
#endif

NLOHMANN_JSON_NAMESPACE_END
