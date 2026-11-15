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
#ifndef XYZ_PROTOCOL_OWNING_VTABLE_HH_
#define XYZ_PROTOCOL_OWNING_VTABLE_HH_

// The vtable a `protocol` calls through. Kept out of `protocol` because any
// name `protocol` declares, private ones included, hides an interface member
// function of that name.

#include <type_traits>
#include <typeinfo>
#include <utility>

#include "xyz/detail/small_buffer.hh"
#include "xyz/detail/vtable.hh"

namespace xyz::detail {

// Extends the generated per-member-function vtable with the entries needed
// for ownership. Because it derives from `storage_vtable_t<I>`, the
// synthesised member function thunks (which take a `const
// storage_vtable_t<I>*`) can call through a `const owning_vtable*` unchanged.
//
// The member function entries take the protocol's `small_buffer_storage`.
// Whether the object is in the small buffer or on the heap is fixed for each
// stored type, so each type's entries know where to find it without a branch.
template <typename I, typename Alloc>
struct owning_vtable : storage_vtable_t<I> {
  // Destroys the object and leaves the storage empty.
  void (*destroy)(const Alloc& alloc, small_buffer_storage& storage) noexcept;
  // Constructs a copy of the object in empty `destination`.
  void (*copy)(const Alloc& alloc, const small_buffer_storage& source,
               small_buffer_storage& destination);
  // Move-constructs the object into empty `destination` through `alloc`,
  // which allocates it unless it is stored in the small buffer.
  void (*move)(const Alloc& alloc, small_buffer_storage& source,
               small_buffer_storage& destination);
  // Moves the object, stored with `alloc`, to empty `destination`, which
  // uses an allocator equal to `alloc`, and leaves the source empty.
  void (*relocate)(const Alloc& alloc, small_buffer_storage& source,
                   small_buffer_storage& destination) noexcept;
  // The vtable a `protocol_view` of the stored object uses: its entries
  // take the object's address, not the storage.
  const vtable_t<I>* object_vtable;
  // Where the object is stored (heap, small buffer, or no object).
  detail::storage_kind storage_kind;
};

// Builds the vtable for the stored type `T`: the member function entries
// call through to `T`'s conforming member functions and the ownership
// entries store it in the small buffer or on the heap (see
// "xyz/detail/small_buffer.hh").
template <typename I, typename Alloc, typename T>
consteval owning_vtable<I, Alloc> make_owning_vtable() {
  using operations = small_buffer_operations<T, Alloc>;

  owning_vtable<I, Alloc> vtable{};
  static_cast<storage_vtable_t<I>&>(vtable) =
      make_vtable<storage_vtable_t<I>, I, T, storage_object_access<T, Alloc>>();

  vtable.destroy = &operations::destroy;

  // Copy construction and assignment should only reach this
  // if the interface is copy constructible.
  vtable.copy = +[](const Alloc& alloc, const small_buffer_storage& source,
                    small_buffer_storage& destination) {
    if constexpr (std::is_copy_constructible_v<I>) {
      operations::copy_construct(alloc, source, destination);
    } else {
      std::unreachable();
    }
  };

  // Move construction and assignment should only reach this
  // if the interface is move constructible.
  vtable.move = +[](const Alloc& alloc, small_buffer_storage& source,
                    small_buffer_storage& destination) {
    if constexpr (std::is_move_constructible_v<I>) {
      operations::move_construct(alloc, source, destination);
    } else {
      std::unreachable();
    }
  };

  // Not limited to move constructible interfaces: copy assignment
  // relocates a copy into place.
  vtable.relocate = &operations::relocate;

  vtable.object_vtable = &view_vtable_for<I, T>;
  vtable.storage_kind =
      operations::fits ? storage_kind::buffer : storage_kind::indirect;

  return vtable;
}

template <typename I, typename Alloc, typename T>
inline constexpr owning_vtable<I, Alloc> owning_vtable_for =
    make_owning_vtable<I, Alloc, std::decay_t<T>>();

// A no-op vtable that is the stand-in for a nullptr vtable. Prevents
// redundant null checks in the special member functions. The member
// function entries are left null: calling a member function on a
// valueless protocol is a precondition violation.
template <typename I, typename Alloc>
consteval owning_vtable<I, Alloc> make_null_owning_vtable() {
  owning_vtable<I, Alloc> result{};
  result.xyz_protocol_typeid = &typeid(void);
  result.destroy = +[](const Alloc&, small_buffer_storage&) noexcept {};
  result.copy = +[](const Alloc&, const small_buffer_storage&,
                    small_buffer_storage& destination) {
    destination.pointer_ = nullptr;
  };
  result.move = +[](const Alloc&, small_buffer_storage&,
                    small_buffer_storage& destination) {
    destination.pointer_ = nullptr;
  };
  result.relocate = +[](const Alloc&, small_buffer_storage&,
                        small_buffer_storage& destination) noexcept {
    destination.pointer_ = nullptr;
  };
  result.object_vtable = &null_view_vtable_for<I>;
  result.storage_kind = storage_kind::none;
  return result;
}

template <typename I, typename Alloc>
inline constexpr owning_vtable<I, Alloc> null_owning_vtable =
    make_null_owning_vtable<I, Alloc>();

}  // namespace xyz::detail

#endif  // XYZ_PROTOCOL_OWNING_VTABLE_HH_
