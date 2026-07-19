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

#include "types.h"

#include <gtest/gtest.h>

#include <meta>
#include <string_view>
#include <type_traits>
#include <vector>

// Direct tests of reflection_detail's type-synthesis helpers (types.h):
// building the R(Ps...) and R(*)(Erased, Ps...) types that thunks and call
// wrappers pattern-match against. This file includes only the types header,
// not protocol.h, and none of the structs below are ever wrapped in
// xyz::protocol.

namespace {

struct SignatureProbe {
  double compute(int, double) { return 0.0; }

  void notify() noexcept {}
};

consteval std::meta::info FindByName(std::string_view name) {
  for (std::meta::info member : std::meta::members_of(
           ^^SignatureProbe, std::meta::access_context::current())) {
    if (std::meta::has_identifier(member) &&
        std::meta::identifier_of(member) == name) {
      return member;
    }
  }
  return std::meta::info{};
}

TEST(ReflectionTypesTest, MemberFunctionTypeMatchesReturnAndParameters) {
  static_assert(
      std::is_same_v<typename[:xyz::reflection_detail::member_function_type(
                                   FindByName("compute")):], double(int,
                                                                    double)>);
}

TEST(ReflectionTypesTest, VtableEntryPointerTypeAddsLeadingErasedPointer) {
  static_assert(std::is_same_v<
                typename[:xyz::reflection_detail::vtable_entry_pointer_type(
                              FindByName("compute"),
                              ^^void*):], double (*)(void*, int, double)>);
}

TEST(ReflectionTypesTest, VtableEntryPointerTypeIsNoexceptWhenMemberIs) {
  static_assert(std::is_same_v<
                typename[:xyz::reflection_detail::vtable_entry_pointer_type(
                              FindByName("notify"),
                              ^^void*):], void (*)(void*) noexcept>);
  static_assert(
      !std::is_same_v<
          typename[:xyz::reflection_detail::vtable_entry_pointer_type(
                        FindByName("compute"),
                        ^^void*):], double (*)(void*, int, double) noexcept>);
}

TEST(ReflectionTypesTest, ParameterTypesOfReturnsTypesInOrder) {
  static_assert(
      xyz::reflection_detail::parameter_types_of(FindByName("compute")) ==
      std::vector<std::meta::info>{^^int, ^^double});
}

struct DataMemberProbe {
  int found_member = 0;
};

TEST(ReflectionTypesTest, DataMemberNamedFindsExistingMember) {
  static_assert(xyz::reflection_detail::data_member_named(
                    ^^DataMemberProbe, "found_member") != std::meta::info{});
}

TEST(ReflectionTypesTest, DataMemberNamedReturnsEmptyForMissingMember) {
  static_assert(xyz::reflection_detail::data_member_named(
                    ^^DataMemberProbe, "no_such_member") == std::meta::info{});
}

}  // namespace
