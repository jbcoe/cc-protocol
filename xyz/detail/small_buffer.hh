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
#include <new>
#include <type_traits>
#include <utility>

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

// As `is_nothrow_storable_v`, when the allocator is also default-constructed.
template <typename T, typename Alloc, typename... Args>
inline constexpr bool is_nothrow_default_storable_v =
    is_nothrow_storable_v<T, Alloc, Args...> &&
    std::is_nothrow_default_constructible_v<Alloc>;

// Selects the constructor that stores the object, which the public
// constructors delegate to.
struct construct_in_storage_t {
  explicit construct_in_storage_t() = default;
};

inline constexpr construct_in_storage_t construct_in_storage{};

// Constructs, accesses, destroys and relocates a `T` held in a
// `small_buffer_storage`: in the buffer if `T` fits, otherwise on the heap
// through the allocator, rebound to `T`.
//
// Constant evaluation cannot place an object in a byte buffer, so a `T` is
// always stored on the heap there. A protocol created during constant
// evaluation cannot outlive it, so a stored object is only ever accessed in
// the kind of evaluation that stored it. Each function below is the only place
// that makes its choice.
template <typename T, typename Alloc>
struct small_buffer_operations {
  using rebound_allocator =
      typename std::allocator_traits<Alloc>::template rebind_alloc<T>;
  using rebound_traits = std::allocator_traits<rebound_allocator>;

  static constexpr bool fits = fits_in_small_buffer_v<T, Alloc>;

  template <typename... Args>
  static constexpr void construct(const Alloc& alloc,
                                  small_buffer_storage& destination,
                                  Args&&... args) {
    if !consteval {
      if constexpr (fits) {
        construct_in_buffer(destination, std::forward<Args>(args)...);
        return;
      }
    }

    rebound_allocator rebound{alloc};
    T* object = rebound_traits::allocate(rebound, 1);
    try {
      rebound_traits::construct(rebound, object, std::forward<Args>(args)...);
    } catch (...) {
      rebound_traits::deallocate(rebound, object, 1);
      throw;
    }
    destination.pointer_ = object;
  }

  static constexpr T* object(small_buffer_storage& storage) noexcept {
    if !consteval {
      if constexpr (fits) {
        return std::launder(reinterpret_cast<T*>(storage.buffer_.data()));
      }
    }
    return static_cast<T*>(storage.pointer_);
  }

  static constexpr const T* object(
      const small_buffer_storage& storage) noexcept {
    if !consteval {
      if constexpr (fits) {
        return std::launder(reinterpret_cast<const T*>(storage.buffer_.data()));
      }
    }
    return static_cast<const T*>(storage.pointer_);
  }

  // Destroys the stored object and leaves `storage` empty.
  static constexpr void destroy(const Alloc& alloc,
                                small_buffer_storage& storage) noexcept {
    if !consteval {
      if constexpr (fits) {
        std::destroy_at(object(storage));
        storage.pointer_ = nullptr;
        return;
      }
    }

    rebound_allocator rebound{alloc};
    T* stored = static_cast<T*>(storage.pointer_);
    rebound_traits::destroy(rebound, stored);
    rebound_traits::deallocate(rebound, stored, 1);
    storage.pointer_ = nullptr;
  }

  static constexpr void copy_construct(const Alloc& alloc,
                                       const small_buffer_storage& source,
                                       small_buffer_storage& destination) {
    construct(alloc, destination, *object(source));
  }

  // Leaves the moved-from object in `source`.
  static constexpr void move_construct(const Alloc& alloc,
                                       small_buffer_storage& source,
                                       small_buffer_storage& destination) {
    construct(alloc, destination, std::move(*object(source)));
  }

  // Moves the stored object from `source` to `destination`, which must be
  // empty, and leaves `source` empty. A heap-allocated object is not moved:
  // its ownership is transferred, so both storages must use equal allocators.
  static constexpr void relocate(small_buffer_storage& source,
                                 small_buffer_storage& destination) noexcept {
    if !consteval {
      if constexpr (fits) {
        T* source_object = object(source);
        construct_in_buffer(destination, std::move(*source_object));
        std::destroy_at(source_object);
        source.pointer_ = nullptr;
        return;
      }
    }
    destination.pointer_ = std::exchange(source.pointer_, nullptr);
  }

 private:
  template <typename... Args>
  static void construct_in_buffer(small_buffer_storage& destination,
                                  Args&&... args) {
    // Make the array the active member. `std::start_lifetime_as` would avoid
    // zero-initializing it, but libc++ does not provide it.
    std::construct_at(&destination.buffer_);
    ::new (static_cast<void*>(destination.buffer_.data()))
        T(std::forward<Args>(args)...);
  }
};

}  // namespace xyz::detail

#endif  // XYZ_PROTOCOL_SMALL_BUFFER_HH_
