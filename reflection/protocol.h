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
#ifndef XYZ_REFLECTION_PROTOCOL_H_
#define XYZ_REFLECTION_PROTOCOL_H_

// A C++26-reflection-based implementation of protocol and protocol_view.

#include <cstddef>
#include <memory>
#include <type_traits>

namespace xyz::reflection {

template <typename T, typename Allocator = std::allocator<std::byte>>
class protocol {
 public:
  // Special member functions.
  protocol()
    requires std::is_default_constructible_v<Allocator> &&
             std::is_default_constructible_v<T>;

  protocol(const protocol&)
    requires std::is_copy_constructible_v<T>;

  protocol(protocol&&);

  protocol& operator=(const protocol&)
    requires std::is_copy_assignable_v<T>;

  protocol& operator=(protocol&&);

  ~protocol();  // Unconstrained.
};

template <typename T>
class protocol_view {
 public:
  // Special member functions.
  protocol_view() = delete;
  protocol_view(const protocol_view&) = default;
  protocol_view(protocol_view&&) noexcept = default;
  protocol_view& operator=(const protocol_view&) = default;
  protocol_view& operator=(protocol_view&&) noexcept = default;
  ~protocol_view() = default;
};

}  // namespace xyz::reflection
#endif  // XYZ_REFLECTION_PROTOCOL_H_
