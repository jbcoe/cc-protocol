/* Copyright (c) 2026 The XYZ Protocol Authors. All Rights Reserved.

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
// Type synthesis helpers for the C++26 reflection backend: building the
// R(Ps...) and R(*)(Erased, Ps...) types that thunks and call wrappers
// pattern-match against, from a reflected member's own return type and
// parameter list. Split out of protocol_reflection.h because this piece is
// self-contained (depends only on the standard library and <meta>, not on
// member enumeration, naming, or vtable machinery) and independently
// testable: types_test.cc includes this header directly rather than the
// whole backend.
#ifndef XYZ_PROTOCOL_REFLECTION_TYPES_H_
#define XYZ_PROTOCOL_REFLECTION_TYPES_H_

#ifndef __cpp_impl_reflection
#error \
    "This header requires a compiler with C++26 reflection support (GCC 16+, -std=c++26 -freflection)."
#endif

#include <meta>
#include <string_view>
#include <vector>

namespace xyz {
namespace reflection_detail {

using std::meta::info;

// ---------------------------------------------------------------------------
// Type synthesis helpers
// ---------------------------------------------------------------------------

template <typename ReturnType, typename... ParameterTypes>
using function_type_for = ReturnType(ParameterTypes...);

template <typename ReturnType, typename... ParameterTypes>
using function_pointer_type_for = ReturnType (*)(ParameterTypes...);

template <typename ReturnType, typename... ParameterTypes>
using noexcept_function_pointer_type_for =
    ReturnType (*)(ParameterTypes...) noexcept;

consteval std::vector<info> parameter_types_of(info member) {
  std::vector<info> result;
  for (info parameter : std::meta::parameters_of(member)) {
    result.push_back(std::meta::type_of(parameter));
  }
  return result;
}

// R(Ps...) for an interface member, used to pattern-match thunks and call
// wrappers so their signatures exactly mirror the interface declaration.
consteval info member_function_type(info member) {
  std::vector<info> arguments;
  arguments.push_back(std::meta::return_type_of(member));
  for (info parameter_type : parameter_types_of(member)) {
    arguments.push_back(parameter_type);
  }
  return std::meta::substitute(^^function_type_for, arguments);
}

// R(*)(ErasedPointer, Ps...) [noexcept] — the type of one vtable entry.
consteval info vtable_entry_pointer_type(info member,
                                         info erased_pointer_type) {
  std::vector<info> arguments;
  arguments.push_back(std::meta::return_type_of(member));
  arguments.push_back(erased_pointer_type);
  for (info parameter_type : parameter_types_of(member)) {
    arguments.push_back(parameter_type);
  }
  return std::meta::substitute(std::meta::is_noexcept(member)
                                   ? ^^noexcept_function_pointer_type_for
                                   : ^^function_pointer_type_for,
                               arguments);
}

consteval info data_member_named(info class_type, std::string_view name) {
  for (info member : std::meta::nonstatic_data_members_of(
           class_type, std::meta::access_context::current())) {
    if (std::meta::has_identifier(member) &&
        std::meta::identifier_of(member) == name) {
      return member;
    }
  }
  return info{};
}

}  // namespace reflection_detail
}  // namespace xyz

#endif  // XYZ_PROTOCOL_REFLECTION_TYPES_H_
