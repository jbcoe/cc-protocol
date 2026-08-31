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

#include <array>
#include <cmath>
#include <meta>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>

#include "consteval_check.h"

// A very brief (and intentionally incomplete) tour of C++26 reflection.

namespace xyz::tutorials {

// A simple type, shared across many tests.
struct Point {
  double x = 0;
  double y = 0;

  double norm() const noexcept { return std::sqrt((x * x) + (y * y)); }
};

}  // namespace xyz::tutorials

namespace xyz::tutorials::introspection {

TEST(TutorialsReflection, MetaInfoIdentifier) {
  // The reflection operator `^^` gets information about the program at
  // compile time.
  constexpr std::meta::info point_info = ^^Point;

  static_assert(identifier_of(point_info) == "Point");
}

TEST(TutorialsReflection, AliasesCompareDifferent) {
  // Only observed through reflection, which the unused-type-alias warning
  // does not count as a use.
  using PointAlias [[maybe_unused]] = Point;

  constexpr std::meta::info point_info = ^^Point;
  constexpr std::meta::info point_alias_info = ^^PointAlias;

  static_assert(point_info != point_alias_info);
  static_assert(point_info == dealias(point_alias_info));
}

TEST(TutorialsReflection, MetaInfoQueries) {
  // std::meta has assorted functions for querying meta-info, found by ADL
  // so used without an std::meta prefix.

  // This list is illustrative, not exhaustive.
  // See: https://en.cppreference.com/cpp/meta/reflection for <meta> functions.
  static_assert(is_namespace(^^xyz));
  static_assert(is_complete_type(^^Point));
  static_assert(!is_complete_type(^^class Incomplete));
  static_assert(is_class_type(^^Point));
  static_assert(!is_function(^^Point));
}

TEST(TutorialsReflection, MembersOfWithConstevalBlock) {
  // `nonstatic_data_members_of` inspects a class's members.

  // We use a check inside a `consteval` block rather than `static_assert`
  // or `EXPECT_*`: `static_assert` needs `members` to independently be a
  // constant expression, and `EXPECT_*`'s comparison helpers aren't
  // `constexpr` functions.
  // The body of a `consteval` block runs at compile time.

  // A bare `throw std::runtime_error("...")` would work too, but the
  // resulting diagnostic only shows the hand-written string, not what was
  // actually compared. `XYZ_CONSTEVAL_CHECK` decomposes the expression and
  // throws `xyz::consteval_check_failure`, whose `what()` carries the
  // `file:line`, the expression text, and the operand values, e.g.
  // "reflection.cc:NN: check failed: identifier_of(members[1]) == \"y\"
  // [\"x\" == \"y\"]". GCC prints `what()` of an uncaught exception thrown
  // during constant evaluation as part of the compile error.
  consteval {
    auto members = nonstatic_data_members_of(
        ^^Point, std::meta::access_context::current());
    XYZ_CONSTEVAL_CHECK(identifier_of(members[0]) == "x");
    XYZ_CONSTEVAL_CHECK(identifier_of(members[1]) == "y");
  }
}

TEST(TutorialsReflection, MembersOfWithDefineStaticArray) {
  // `nonstatic_data_members_of` returns a `std::vector` of `std::meta::info`.
  // Its backing allocation must be freed within the same constant
  // evaluation that created it, so it can't persist as a constexpr value
  // across evaluations; `define_static_array` copies the data into static
  // storage instead.

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
  // range to functions, and `has_identifier` further narrows it to ones
  // `identifier_of` accepts.

  constexpr std::ranges::range auto member_functions = std::define_static_array(
      members_of(^^Point, std::meta::access_context::current()) |
      std::views::filter(std::meta::is_function) |
      std::views::filter(std::meta::has_identifier));

  constexpr std::meta::info norm = member_functions[0];

  static_assert(identifier_of(norm) == "norm");
  static_assert(return_type_of(norm) == ^^double);
  static_assert(parameters_of(norm).empty());
  static_assert(is_const(norm));
  static_assert(is_noexcept(norm));
}

TEST(TutorialsReflection, EnumeratorsOfWithDefineStaticArray) {
  // `enumerators_of` returns a `std::vector` of `std::meta::info`, one per
  // enumerator, with the same allocation restriction as
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
    // Only observed through reflection, which the unused-private-field
    // warning does not count as a use.
    [[maybe_unused]] int hidden;
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

// C++26 reflection also supports code generation.

namespace xyz::tutorials::code_generation {

TEST(TutorialsReflection, SpliceTypeFromInfo) {
  // `typename [: info :]` splices an info back into the type it reflects.
  constexpr std::meta::info point_info = ^^Point;

  // `[: :]` splices both types and values; `typename` disambiguates which.
  static_assert(std::is_same_v<typename[:point_info:], Point>);
}

TEST(TutorialsReflection, SpliceValueFromInfo) {
  // `[: :]` also splices a value.
  enum class Color { Red, Green, Blue };

  constexpr std::meta::info green_info = ^^Color::Green;
  constexpr Color green = [:green_info:];

  static_assert(green == Color::Green);
}

TEST(TutorialsReflection, SpliceCallToMember) {
  // `obj.[:member_info:](args)` calls whichever member `member_info` reflects.
  constexpr auto norm = ^^Point::norm;

  Point point{.x = 3, .y = 4};
  EXPECT_EQ(point.[:norm:](), point.norm());
}

template <typename E>
  requires std::is_enum_v<E>
std::string enum_to_string(E value) {
  // `template for` unrolls a compile-time range into ordinary code.
  // NOLINTBEGIN(bugprone-reserved-identifier): fires on the compiler's own
  // internal variables in the expansion statement's desugaring.
  template for (constexpr std::meta::info e :
                std::define_static_array(enumerators_of(^^E))) {
    // NOLINTEND(bugprone-reserved-identifier)
    if (value == [:e:]) return std::string(identifier_of(e));
  }
  return "<unknown>";
}

enum class Suit { Clubs, Diamonds, Hearts, Spades };

TEST(TutorialsReflection, EnumToString) {
  EXPECT_EQ(enum_to_string(Suit::Clubs), "Clubs");
  EXPECT_EQ(enum_to_string(Suit::Diamonds), "Diamonds");
  EXPECT_EQ(enum_to_string(Suit::Hearts), "Hearts");
  EXPECT_EQ(enum_to_string(Suit::Spades), "Spades");
}

consteval std::optional<Suit> suit_from_name(std::string_view name) {
  for (std::meta::info e : enumerators_of(^^Suit)) {
    if (identifier_of(e) == name) {
      // `e` is a loop variable, not a constant expression, so we can't
      // splice it with `return [:e:];`. `extract<Suit>(e)` only needs a
      // value, not a fixed compile-time constant.
      return extract<Suit>(e);
    }
  }
  return std::nullopt;
}

TEST(TutorialsReflection, Extract) {
  static_assert(suit_from_name("Hearts") == Suit::Hearts);
  static_assert(suit_from_name("Joker") == std::nullopt);
}

TEST(TutorialsReflection, DefineAggregate) {
  // `define_aggregate` gives an incomplete class type real data members,
  // built from a `vector<data_member_spec>` instead of hand-written struct
  // syntax; `data_member_options` names each member.
  // `no_unique_address` mirrors the `[[no_unique_address]]` attribute,
  // letting an empty member avoid inflating the type's size.
  struct Empty {};
  struct Synthesized;
  // clang-format off
  consteval {
    define_aggregate(^^Synthesized, {
        data_member_spec(^^int, {.name = "value"}),
        data_member_spec(^^Empty,
                          {.name = "empty", .no_unique_address = true}),
    });
  }
  // clang-format on

  constexpr std::ranges::range auto members =
      std::define_static_array(nonstatic_data_members_of(
          ^^Synthesized, std::meta::access_context::unchecked()));

  static_assert(members.size() == 2);
  static_assert(identifier_of(members[0]) == "value");
  static_assert(identifier_of(members[1]) == "empty");

  static_assert(sizeof(Synthesized) == sizeof(int));
}

TEST(TutorialsReflection, Substitute) {
  // A variable that exists during constant evaluation is not necessarily
  // constexpr. A `std::meta::info` local variable cannot always be plugged into
  // a template (e.g. `MyType<typename [: myLocal :]>`) since it may not be
  // constexpr. To workaround this, we can use `substitute`.
  // `reflect_constant` turns an ordinary value into an info usable as a
  // non-type template argument.
  // clang-format off
  constexpr std::meta::info array_info =
      substitute(^^std::array, {^^double, std::meta::reflect_constant(3)});
  // clang-format on

  static_assert(std::is_same_v<typename[:array_info:], std::array<double, 3>>);
}

TEST(TutorialsReflection, DisplayStringOf) {
  // `display_string_of` returns an implementation-defined string.
  // Another implementation could choose to display `double` differently.
  static_assert(display_string_of(^^double) == "double");
}

}  // namespace xyz::tutorials::code_generation
