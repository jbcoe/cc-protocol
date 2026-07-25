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
#include "protocol_reflection_detail/types.hxx"

#include <gtest/gtest.h>

#include <meta>
#include <type_traits>

namespace {

using xyz::reflection_detail::member_function_type;
using xyz::reflection_detail::parameter_types_of;
using xyz::reflection_detail::vtable_entry_pointer_type;

struct Fixture {
  int compute(double x, int y) const { return static_cast<int>(x) + y; }

  void set_value(int) {}

  void no_args() {}
};

TEST(ParameterTypesOf, ListsEachParameterInOrder) {
  constexpr auto types =
      std::define_static_array(parameter_types_of(^^Fixture::compute));
  static_assert(types.size() == 2);
  static_assert(types[0] == ^^double);
  static_assert(types[1] == ^^int);
}

TEST(ParameterTypesOf, EmptyForANoArgumentMember) {
  constexpr auto types =
      std::define_static_array(parameter_types_of(^^Fixture::no_args));
  static_assert(types.size() == 0);
}

TEST(MemberFunctionType, BuildsTheMembersOwnCallSignature) {
  static_assert(
      std::is_same_v<typename[:member_function_type(
                                   ^^Fixture::compute):], int(double, int)>);
  static_assert(
      std::is_same_v<
          typename[:member_function_type(^^Fixture::set_value):], void(int)>);
  static_assert(std::is_same_v<
                typename[:member_function_type(^^Fixture::no_args):], void()>);
}

TEST(VtableEntryPointerType,
     ReplacesTheImplicitObjectParameterWithAnErasedPointer) {
  static_assert(
      std::is_same_v<typename[:vtable_entry_pointer_type(
                                   ^^Fixture::compute,
                                   ^^const void*):], int (*)(const void*,
                                                             double, int)>);
  static_assert(std::is_same_v<typename[:vtable_entry_pointer_type(
                                             ^^Fixture::set_value,
                                             ^^void*):], void (*)(void*, int)>);
  static_assert(std::is_same_v<
                typename[:vtable_entry_pointer_type(
                              ^^Fixture::no_args, ^^void*):], void (*)(void*)>);
}

}  // namespace
