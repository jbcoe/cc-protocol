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

#if defined(__cpp_constexpr) && __cpp_constexpr >= 202306L
// Casting void* to T* in a constexpr context requires
// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p2738r1.pdf
#define XYZ_2026_CONSTEXPR constexpr
#else
#define XYZ_2026_CONSTEXPR
#endif

#include <memory>

namespace xyz {

template <typename T, typename A = std::allocator<T>>
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
      "  OUTPUT: The path where the generated header should be written.\n"
      "  MANUAL_VTABLE (optional): If set, use the manual vtable template.\n\n"
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

}  // namespace xyz

#endif  // XYZ_PROTOCOL_H_
