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
#ifndef XYZ_PROTOCOL_H_
#define XYZ_PROTOCOL_H_
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace xyz {

template <typename T>
struct is_protocol : std::false_type {};

template <typename T, typename Alloc = std::allocator<T>>
class protocol;

template <typename T, typename Alloc>
struct is_protocol<protocol<T, Alloc>> : std::true_type {};

template <typename T>
struct is_protocol_view : std::false_type {};

template <typename T>
class protocol_view;

template <typename T>
struct is_protocol_view<protocol_view<T>> : std::true_type {};

template <typename T>
concept not_protocol_or_view = !is_protocol<std::remove_cvref_t<T>>::value &&
                               !is_protocol_view<std::remove_cvref_t<T>>::value;

template <typename Protocol>
struct protocol_vtable_traits;

const void* get_mapped_vtable(const void* source_vtable_pointer,
                              const void* conversion_anchor,
                              std::size_t target_vtable_size,
                              void (*mapping_function)(const void* source,
                                                       void* target));

template <typename FromProtocol, typename ToProtocol>
const typename protocol_vtable_traits<ToProtocol>::const_vtable* get_vtable(
    const typename protocol_vtable_traits<FromProtocol>::const_vtable*
        source_vtable_pointer) {
  using FromVtable =
      typename protocol_vtable_traits<FromProtocol>::const_vtable;
  using ToVtable = typename protocol_vtable_traits<ToProtocol>::const_vtable;

  static const char conversion_anchor = 0;

  auto mapping_function = [](const void* source, void* target) {
    map_vtable_members(static_cast<const FromVtable*>(source),
                       static_cast<ToVtable*>(target));
  };

  return static_cast<const ToVtable*>(
      get_mapped_vtable(source_vtable_pointer, &conversion_anchor,
                        sizeof(ToVtable), mapping_function));
}

template <typename FromProtocol, typename ToProtocol>
const typename protocol_vtable_traits<ToProtocol>::vtable* get_mutable_vtable(
    const typename protocol_vtable_traits<FromProtocol>::vtable*
        source_vtable_pointer) {
  using FromVtable = typename protocol_vtable_traits<FromProtocol>::vtable;
  using ToVtable = typename protocol_vtable_traits<ToProtocol>::vtable;

  static const char conversion_anchor = 0;

  auto mapping_function = [](const void* source, void* target) {
    map_mutable_vtable_members(static_cast<const FromVtable*>(source),
                               static_cast<ToVtable*>(target));
  };

  return static_cast<const ToVtable*>(
      get_mapped_vtable(source_vtable_pointer, &conversion_anchor,
                        sizeof(ToVtable), mapping_function));
}

template <typename Protocol, typename Allocator>
struct protocol_owning_vtable_traits;

template <typename FromProtocol, typename ToProtocol, typename Allocator>
const typename protocol_owning_vtable_traits<ToProtocol, Allocator>::vtable*
get_owning_vtable(const typename protocol_owning_vtable_traits<
                  FromProtocol, Allocator>::vtable* source_vtable_pointer) {
  if (source_vtable_pointer == nullptr) {
    return nullptr;
  }
  using FromVtable =
      typename protocol_owning_vtable_traits<FromProtocol, Allocator>::vtable;
  using ToVtable =
      typename protocol_owning_vtable_traits<ToProtocol, Allocator>::vtable;

  static const char conversion_anchor = 0;

  auto mapping_function = [](const void* source, void* target) {
    map_owning_vtable_members(static_cast<const FromVtable*>(source),
                              static_cast<ToVtable*>(target));
  };

  return static_cast<const ToVtable*>(
      get_mapped_vtable(source_vtable_pointer, &conversion_anchor,
                        sizeof(ToVtable), mapping_function));
}

#if !(defined(__cpp_impl_reflection) && defined(XYZ_PROTOCOL_ENABLE_REFLECTION))
template <typename T, typename A>
class protocol {
  static_assert(
      sizeof(T) == 0,
      "The primary xyz::protocol template cannot be instantiated. "
      "A partial specialization for T must be generated as a build "
      "step. Use the xyz_generate_protocol CMake macro to produce "
      "the required specialization.\n\n"
      "Arguments:\n"
      "  CLASS_NAME: The name of the class to be generated.\n"
      "  INTERFACE: The path to the header file containing the interface "
      "definition.\n"
      "  HEADER: The name of the header file to be included in the generated "
      "file.\n"
      "  OUTPUT: The path where the generated header should be written.\n\n"
      "Example usage:\n"
      "  xyz_generate_protocol(\n"
      "    CLASS_NAME MyInterface\n"
      "    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/my_interface.h\n"
      "    HEADER my_interface.h\n"
      "    OUTPUT "
      "${CMAKE_CURRENT_BINARY_DIR}/generated/protocol_MyInterface.h\n"
      "  )");

 public:
  constexpr bool valueless_after_move() const noexcept {
    return false;  // Placeholder implementation
  }
};

template <typename T>
class protocol_view {
  static_assert(
      sizeof(T) == 0,
      "The primary xyz::protocol_view template cannot be instantiated. "
      "A partial specialization for T must be generated as a build "
      "step. Use the xyz_generate_protocol CMake macro to produce "
      "the required specialization.\n\n"
      "Note: protocol_view specializations are automatically generated "
      "alongside protocol specializations.");
};
#endif

}  // namespace xyz

// Pulls in the C++26-reflection code-generation backend, which defines the
// real xyz::protocol / xyz::protocol_view primary templates in place of the
// placeholder ones above. On a compiler or build configuration where
// reflection isn't enabled, protocol_reflection.h is an inert no-op.
#include "protocol_reflection.h"

#endif  // XYZ_PROTOCOL_H_
