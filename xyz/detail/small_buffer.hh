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
#ifndef XYZ_PROTOCOL_SMALL_BUFFER_HH_
#define XYZ_PROTOCOL_SMALL_BUFFER_HH_

// Storage for the object a `protocol` owns: either the object itself, in a
// small buffer, or a pointer to it on the heap.

#include <array>
#include <cstddef>
#include <memory>
#include <type_traits>

namespace xyz::detail {

// The buffer is the same for every interface so that a type has the same
// storage kind in every protocol.
inline constexpr std::size_t small_buffer_size = 3 * sizeof(void*);

inline constexpr std::size_t small_buffer_alignment = alignof(void*);

union small_buffer_storage {
  void* pointer_ = nullptr;
  alignas(
      small_buffer_alignment) std::array<std::byte, small_buffer_size> buffer_;
};

// Objects in the buffer are constructed without the allocator, so
// allocator-aware types are always stored on the heap: whether a type
// receives the allocator does not depend on its size.
template <typename T, typename Alloc>
inline constexpr bool fits_in_small_buffer_v =
    sizeof(T) <= small_buffer_size && alignof(T) <= small_buffer_alignment &&
    std::is_nothrow_move_constructible_v<T> && !std::uses_allocator_v<T, Alloc>;

// Storing a `T` constructed from `Args...` cannot throw: it fits in the buffer,
// so nothing is allocated, and its constructor does not throw.
template <typename T, typename Alloc, typename... Args>
inline constexpr bool is_nothrow_storable_v =
    fits_in_small_buffer_v<T, Alloc> &&
    std::is_nothrow_constructible_v<T, Args...>;

}  // namespace xyz::detail

#endif  // XYZ_PROTOCOL_SMALL_BUFFER_HH_
