/* Copyright (c) 2025 The XYZ Protocol Authors. All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
==============================================================================*/
#ifndef XYZ_PROTOCOL_PROTOCOL_TRAITS_HH_
#define XYZ_PROTOCOL_PROTOCOL_TRAITS_HH_

#include <algorithm>
#include <concepts>
#include <meta>
#include <ranges>
#include <type_traits>

namespace xyz::reflection {

template <typename I>
concept is_valid_interface =
    is_class_type(^^I) && std::same_as<I, std::remove_cvref_t<I>> &&
    std::ranges::none_of(
        members_of(^^I, std::meta::access_context::unprivileged()),
        std::meta::is_volatile);

template <typename I>
concept is_valid_view_interface =
    is_valid_interface<I> || is_valid_interface<std::remove_const_t<I>>;

template <is_valid_interface T, typename Allocator>
class protocol;

template <is_valid_view_interface T>
class protocol_view;

template <typename T>
struct is_protocol : std::false_type {};

template <is_valid_interface T, typename Allocator>
struct is_protocol<protocol<T, Allocator>> : std::true_type {};

template <typename T>
inline constexpr bool is_protocol_v = is_protocol<T>::value;

template <typename T>
struct is_protocol_view : std::false_type {};

template <is_valid_view_interface T>
struct is_protocol_view<protocol_view<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_protocol_view_v = is_protocol_view<T>::value;

}  // namespace xyz::reflection

#endif  // XYZ_PROTOCOL_PROTOCOL_TRAITS_HH_
