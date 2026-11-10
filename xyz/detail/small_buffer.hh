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

inline constexpr std::size_t small_buffer_size = 3 * sizeof(void*);

inline constexpr std::size_t small_buffer_alignment = alignof(void*);

union small_buffer_storage {
  void* pointer_ = nullptr;
  alignas(
      small_buffer_alignment) std::array<std::byte, small_buffer_size> buffer_;
};

// The allocator that constructs, destroys and, on the heap, allocates a
// stored `T`.
template <typename T, typename Alloc>
using rebound_allocator_t =
    typename std::allocator_traits<Alloc>::template rebind_alloc<T>;

// `Alloc` has a `construct` member that `std::allocator_traits<Alloc>` calls
// to construct a `T` from `Args...`.
template <typename Alloc, typename T, typename... Args>
concept has_construct_member =
    requires(Alloc& alloc, T* object, Args&&... args) {
      alloc.construct(object, std::forward<Args>(args)...);
    };

// Constructing a `T` from `Args...` with
// `std::allocator_traits<Alloc>::construct` cannot throw. That function is
// not declared `noexcept`, so this asks what it calls: `Alloc::construct` if
// there is one, otherwise `T`'s constructor.
template <typename Alloc, typename T, typename... Args>
inline constexpr bool is_nothrow_allocator_constructible_v = [] {
  if constexpr (has_construct_member<Alloc, T, Args...>) {
    return noexcept(std::declval<Alloc&>().construct(std::declval<T*>(),
                                                     std::declval<Args>()...));
  } else {
    return std::is_nothrow_constructible_v<T, Args...>;
  }
}();

template <typename T, typename Alloc>
inline constexpr bool is_nothrow_allocator_move_constructible_v = [] consteval {
  if constexpr (std::is_move_constructible_v<T>) {
    return is_nothrow_allocator_constructible_v<rebound_allocator_t<T, Alloc>,
                                                T, T&&>;
  } else {
    return false;
  }
}();

// A `T` is stored in the buffer if it fits and relocating it cannot throw.
// Relocation, which protocol's `noexcept` move constructor and swap rely on,
// constructs the moved object through the allocator, so an allocator whose
// `construct` can throw, such as `std::pmr::polymorphic_allocator`, never
// uses the buffer.
template <typename T, typename Alloc>
inline constexpr bool fits_in_small_buffer_v =
    sizeof(T) <= small_buffer_size && alignof(T) <= small_buffer_alignment &&
    is_nothrow_allocator_move_constructible_v<T, Alloc>;

// Storing a `T` constructed from `Args...` cannot throw: it fits in the buffer,
// so nothing is allocated, and constructing it through the allocator does not
// throw.
template <typename T, typename Alloc, typename... Args>
inline constexpr bool is_nothrow_storable_v =
    fits_in_small_buffer_v<T, Alloc> &&
    is_nothrow_allocator_constructible_v<rebound_allocator_t<T, Alloc>, T,
                                         Args...>;

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
// `small_buffer_storage`: in the buffer if `T` fits, otherwise on the heap.
// Either way the object is constructed and destroyed through the allocator,
// rebound to `T`; only an object on the heap is allocated by it.
//
// Constant evaluation cannot place an object in a byte buffer, so a `T` is
// always stored on the heap there. A protocol created during constant
// evaluation cannot outlive it, so a stored object is only ever accessed in
// the kind of evaluation that stored it. Each function below is the only place
// that makes its choice.
template <typename T, typename Alloc>
struct small_buffer_operations {
  using rebound_allocator = rebound_allocator_t<T, Alloc>;
  using rebound_traits = std::allocator_traits<rebound_allocator>;

  static constexpr bool fits = fits_in_small_buffer_v<T, Alloc>;

  template <typename... Args>
  static constexpr void construct(const Alloc& alloc,
                                  small_buffer_storage& destination,
                                  Args&&... args) {
    if !consteval {
      if constexpr (fits) {
        construct_in_buffer(alloc, destination, std::forward<Args>(args)...);
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
        rebound_allocator rebound{alloc};
        rebound_traits::destroy(rebound, object(storage));
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
  // empty, and leaves `source` empty. `alloc` is the allocator the object was
  // stored with. A heap-allocated object is not moved: its ownership is
  // transferred, so both storages must use allocators equal to `alloc`.
  static constexpr void relocate([[maybe_unused]] const Alloc& alloc,
                                 small_buffer_storage& source,
                                 small_buffer_storage& destination) noexcept {
    if !consteval {
      if constexpr (fits) {
        T* source_object = object(source);
        construct_in_buffer(alloc, destination, std::move(*source_object));
        rebound_allocator rebound{alloc};
        rebound_traits::destroy(rebound, source_object);
        source.pointer_ = nullptr;
        return;
      }
    }
    destination.pointer_ = std::exchange(source.pointer_, nullptr);
  }

 private:
  template <typename... Args>
  static void construct_in_buffer(const Alloc& alloc,
                                  small_buffer_storage& destination,
                                  Args&&... args) {
    // Start the array's lifetime, making it the active member, so that it
    // provides storage for the `T`. This zero-fills the buffer, which costs a
    // few stores on every construction and relocation into it. Leaving the
    // bytes the object does not fill uninitialized makes GCC's
    // -Wmaybe-uninitialized report member function calls.
    std::construct_at(&destination.buffer_);
    rebound_allocator rebound{alloc};
    rebound_traits::construct(rebound,
                              reinterpret_cast<T*>(destination.buffer_.data()),
                              std::forward<Args>(args)...);
  }
};

// Finds the `T` that a vtable entry is passed a `small_buffer_storage*` for.
template <typename T, typename Alloc>
struct storage_object_access {
  using operations = small_buffer_operations<T, Alloc>;

  static T* object(small_buffer_storage* storage) noexcept {
    return operations::object(*storage);
  }

  static const T* object(const small_buffer_storage* storage) noexcept {
    return operations::object(*storage);
  }
};

}  // namespace xyz::detail

#endif  // XYZ_PROTOCOL_SMALL_BUFFER_HH_
