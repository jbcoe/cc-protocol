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
#include "protocol_reflection_detail/members.hxx"

#include <gtest/gtest.h>

#include <meta>
#include <string_view>

namespace {

using xyz::reflection_detail::interface_member_functions;
using xyz::reflection_detail::is_interface_member_function;

struct Fixture {
  Fixture() = default;

  int value() const { return 0; }

  void set_value(int) {}

  Fixture operator+(const Fixture& other) const { return other; }

  static void static_helper() {}

  template <typename T>
  void templated(T) {}

 private:
  void private_helper() {}

 public:
  int data_member = 0;
};

TEST(IsInterfaceMemberFunction, AcceptsOrdinaryPublicMembers) {
  static_assert(is_interface_member_function(^^Fixture::value));
  static_assert(is_interface_member_function(^^Fixture::set_value));
}

TEST(IsInterfaceMemberFunction, AcceptsOrdinaryOperatorOverloads) {
  // operator+ is an ordinary (non-special) operator: an interface member
  // function like any other, just found by kind rather than by name.
  bool found_ordinary_operator = false;
  template for (constexpr std::meta::info member :
                std::define_static_array(std::meta::members_of(
                    ^^Fixture, std::meta::access_context::current()))) {
    if (std::meta::is_operator_function(member) &&
        !std::meta::is_special_member_function(member)) {
      EXPECT_TRUE(is_interface_member_function(member));
      found_ordinary_operator = true;
    }
  }
  EXPECT_TRUE(found_ordinary_operator);
}

TEST(IsInterfaceMemberFunction, RejectsSpecialOperatorsLikeAssignment) {
  // Fixture's implicit copy/move assignment operators are both "operator
  // functions" and "special member functions" at once; the special-member
  // exclusion applies regardless of the operator check, unlike operator+.
  bool found_special_operator = false;
  template for (constexpr std::meta::info member :
                std::define_static_array(std::meta::members_of(
                    ^^Fixture, std::meta::access_context::current()))) {
    if (std::meta::is_operator_function(member) &&
        std::meta::is_special_member_function(member)) {
      EXPECT_FALSE(is_interface_member_function(member));
      found_special_operator = true;
    }
  }
  EXPECT_TRUE(found_special_operator);
}

TEST(IsInterfaceMemberFunction, RejectsSpecialMemberFunctions) {
  // A constructor's name denotes an overload set, so it can't be reflected
  // directly (^^Fixture::Fixture); find it by walking members and matching
  // on is_constructor instead.
  bool found_constructor = false;
  template for (constexpr std::meta::info member :
                std::define_static_array(std::meta::members_of(
                    ^^Fixture, std::meta::access_context::current()))) {
    if (std::meta::is_constructor(member)) {
      EXPECT_FALSE(is_interface_member_function(member));
      found_constructor = true;
    }
  }
  EXPECT_TRUE(found_constructor);
}

TEST(IsInterfaceMemberFunction, RejectsStaticMembers) {
  static_assert(!is_interface_member_function(^^Fixture::static_helper));
}

TEST(IsInterfaceMemberFunction, RejectsMemberFunctionTemplates) {
  static_assert(!is_interface_member_function(^^Fixture::templated));
}

TEST(IsInterfaceMemberFunction, RejectsDataMembers) {
  static_assert(!is_interface_member_function(^^Fixture::data_member));
}

TEST(InterfaceMemberFunctions, FindsExactlyTheDispatchableMembers) {
  // value, set_value, and operator+: not the constructor, static_helper,
  // templated, private_helper (inaccessible from this scope), or
  // data_member.
  static constexpr auto members =
      std::define_static_array(interface_member_functions(^^Fixture));
  static_assert(members.size() == 3);

  bool has_value = false;
  bool has_set_value = false;
  bool has_operator = false;
  template for (constexpr std::meta::info member : members) {
    // if constexpr, not if: identifier_of is consteval, so a plain if's
    // untaken branch would still have to evaluate it for operator+
    // (which has no identifier) even though that branch never runs.
    if constexpr (!std::meta::has_identifier(member)) {
      has_operator = true;
    } else {
      std::string_view name = std::meta::identifier_of(member);
      if (name == "value") has_value = true;
      if (name == "set_value") has_set_value = true;
    }
  }
  EXPECT_TRUE(has_value);
  EXPECT_TRUE(has_set_value);
  EXPECT_TRUE(has_operator);
}

}  // namespace
