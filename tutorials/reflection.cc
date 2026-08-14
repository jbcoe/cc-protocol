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

#include <gtest/gtest.h>

#include <cmath>
#include <meta>
#include <ranges>

// A very brief (and intentionally incomplete) tour of C++26 reflection.

namespace xyz::tutorials::introspection {

struct Point {
  double x = 0;
  double y = 0;

  double norm() const noexcept { return std::sqrt(x * x + y * y); }
};

TEST(TutorialsReflection, MetaInfoIdentifier) {
  // The reflection operator `^^` can be used to get information about the
  // program at compile time.
  constexpr std::meta::info point_info = ^^Point;

  static_assert(identifier_of(point_info) == "Point");
}

TEST(TutorialsReflection, AliasesCompareDifferent) {
  using PointAlias = Point;

  constexpr std::meta::info point_info = ^^Point;
  constexpr std::meta::info point_alias_info = ^^PointAlias;

  static_assert(point_info != point_alias_info);
  static_assert(point_info == dealias(point_alias_info));
}

TEST(TutorialsReflection, MetaInfoQueries) {
  // std::meta has assorted functions for querying meta-info.
  // The functions can be found by ADL so need no std::meta prefix.

  // This list is illustrative, not exhaustive.
  // See: https://en.cppreference.com/cpp/meta/reflection for <meta> functions.
  static_assert(is_namespace(^^xyz));
  static_assert(is_complete_type(^^Point));
  static_assert(!is_complete_type(^^class Incomplete));
  static_assert(is_class_type(^^Point));
  static_assert(!is_function(^^Point));
}

TEST(TutorialsReflection, MembersOfWithConstevalBlock) {
  // `nonstatic_data_members_of` allows inspection of class members.

  // We use `throw` inside a `consteval` block rather than `static_assert`
  // or `EXPECT_*`: `static_assert` needs `members` to independently be a
  // constant expression, and gtest's comparison helpers used by `EXPECT_*`
  // aren't `constexpr` functions.

  consteval {
    auto members = nonstatic_data_members_of(
        ^^Point, std::meta::access_context::current());
    if (identifier_of(members[0]) != "x") {
      throw "members[0] is not \"x\"";
    }
    if (identifier_of(members[1]) != "y") {
      throw "members[1] is not \"y\"";
    }
  }
}

TEST(TutorialsReflection, MembersOfWithDefineStaticArray) {
  // `nonstatic_data_members_of` returns a `std::vector` of `std::meta::info`
  // backed by a transient constexpr allocation, which can't persist as a
  // constexpr value across evaluations; `define_static_array` copies the data
  // into static storage instead.

  constexpr std::ranges::range auto members = std::define_static_array(
      nonstatic_data_members_of(^^Point, std::meta::access_context::current()));

  static_assert(identifier_of(members[0]) == "x");
  static_assert(identifier_of(members[1]) == "y");

  // `type_of` returns the reflection of a member's declared type.
  static_assert(type_of(members[0]) == ^^double);
  static_assert(type_of(members[1]) == ^^double);
}

TEST(TutorialsReflection, MemberFunctions) {
  // `members_of` returns every member, including implicitly-declared special
  // member functions that have no identifier; `is_function` narrows the
  // range to functions, and `has_identifier` further narrows it to the ones
  // `identifier_of` can be called on.

  constexpr std::ranges::range auto member_functions = std::define_static_array(
      members_of(^^Point, std::meta::access_context::current()) |
      std::views::filter([](std::meta::info member) {
        return is_function(member) && has_identifier(member);
      }));

  constexpr std::meta::info norm = member_functions[0];

  static_assert(identifier_of(norm) == "norm");
  static_assert(return_type_of(norm) == ^^double);
  static_assert(parameters_of(norm).size() == 0);
  static_assert(is_const(norm));
  static_assert(is_noexcept(norm));
}

TEST(TutorialsReflection, EnumeratorsOfWithDefineStaticArray) {
  // `enumerators_of` returns a `std::vector` of `std::meta::info`, one per
  // enumerator, with the same transient-constexpr-allocation restriction as
  // `nonstatic_data_members_of` above.

  enum class Color { Red, Green, Blue };

  constexpr std::ranges::range auto enumerators =
      std::define_static_array(enumerators_of(^^Color));

  static_assert(identifier_of(enumerators[0]) == "Red");
  static_assert(identifier_of(enumerators[1]) == "Green");
  static_assert(identifier_of(enumerators[2]) == "Blue");
}

TEST(TutorialsReflection, AccessContext) {
  // `access_context` controls which members a query is allowed to see.
  // `current()` uses the access rules of the code position where it is called.
  // `unprivileged()` is restricted to public members.
  // `unchecked()` disables access checking entirely.
  struct AccessContextExample {
   public:
    int visible;

   private:
    int hidden;
  };

  constexpr std::ranges::range auto visible_members =
      std::define_static_array(nonstatic_data_members_of(
          ^^AccessContextExample, std::meta::access_context::current()));

  static_assert(visible_members.size() == 1);
  static_assert(identifier_of(visible_members[0]) == "visible");

  constexpr std::ranges::range auto all_members =
      std::define_static_array(nonstatic_data_members_of(
          ^^AccessContextExample, std::meta::access_context::unchecked()));

  static_assert(all_members.size() == 2);
  static_assert(identifier_of(all_members[0]) == "visible");
  static_assert(identifier_of(all_members[1]) == "hidden");
}

}  // namespace xyz::tutorials::introspection
