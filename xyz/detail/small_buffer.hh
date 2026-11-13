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

// Storage for the object a `protocol` owns.

#include <memory>
#include <utility>

namespace xyz::detail {

union small_buffer_storage {
  void* pointer_ = nullptr;
};

// Where a `protocol`'s object is stored: nowhere, if the protocol is
// valueless, or on the heap.
enum class storage_kind {
  none,
  indirect,
};

// Selects the constructor that stores the object, which the public
// constructors delegate to.
struct construct_in_storage_t {
  explicit construct_in_storage_t() = default;
};

inline constexpr construct_in_storage_t construct_in_storage{};

// Constructs, accesses, destroys and relocates a `T` held in a
// `small_buffer_storage`, on the heap through the allocator, rebound to `T`.
template <typename T, typename Alloc>
struct small_buffer_operations {
  using rebound_allocator =
      typename std::allocator_traits<Alloc>::template rebind_alloc<T>;
  using rebound_traits = std::allocator_traits<rebound_allocator>;

  template <typename... Args>
  static constexpr void construct(const Alloc& alloc,
                                  small_buffer_storage& destination,
                                  Args&&... args) {
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
    return static_cast<T*>(storage.pointer_);
  }

  static constexpr const T* object(
      const small_buffer_storage& storage) noexcept {
    return static_cast<const T*>(storage.pointer_);
  }

  // Destroys the stored object and leaves `storage` empty.
  static constexpr void destroy(const Alloc& alloc,
                                small_buffer_storage& storage) noexcept {
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
    destination.pointer_ = std::exchange(source.pointer_, nullptr);
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
