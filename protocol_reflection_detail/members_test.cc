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

#include "members.h"

#include <gtest/gtest.h>

#include <string_view>

// Direct tests of reflection_detail's member-enumeration helpers
// (members.h): which of a struct's members count as dispatchable interface
// members at all. This file includes only the members header, not
// protocol.h, and none of the structs below are ever wrapped in
// xyz::protocol.

namespace {

struct EnumerationProbe {
  EnumerationProbe() = default;

  void public_method() {}

 private:
  void private_method() {}

 public:
  static void static_method() {}

  template <typename T>
  void template_method(T) {}

  int operator+(int x) { return x; }

  int data_member = 0;
};

consteval std::meta::info FindByName(std::string_view name) {
  for (std::meta::info member : std::meta::members_of(
           ^^EnumerationProbe, std::meta::access_context::unchecked())) {
    if (std::meta::has_identifier(member) &&
        std::meta::identifier_of(member) == name) {
      return member;
    }
  }
  return std::meta::info{};
}

consteval std::meta::info FindSpecialMember() {
  for (std::meta::info member : std::meta::members_of(
           ^^EnumerationProbe, std::meta::access_context::unchecked())) {
    if (std::meta::is_special_member_function(member)) return member;
  }
  return std::meta::info{};
}

consteval std::meta::info FindOperatorPlus() {
  for (std::meta::info member : std::meta::members_of(
           ^^EnumerationProbe, std::meta::access_context::unchecked())) {
    if (std::meta::is_operator_function(member) &&
        std::meta::operator_of(member) == std::meta::operators::op_plus) {
      return member;
    }
  }
  return std::meta::info{};
}

TEST(ReflectionMembersTest, AcceptsOrdinaryPublicMethod) {
  static_assert(xyz::reflection_detail::is_interface_member_function(
      FindByName("public_method")));
}

TEST(ReflectionMembersTest, RejectsPrivateMethod) {
  static_assert(!xyz::reflection_detail::is_interface_member_function(
      FindByName("private_method")));
}

TEST(ReflectionMembersTest, RejectsStaticMethod) {
  static_assert(!xyz::reflection_detail::is_interface_member_function(
      FindByName("static_method")));
}

TEST(ReflectionMembersTest, RejectsFunctionTemplate) {
  static_assert(!xyz::reflection_detail::is_interface_member_function(
      FindByName("template_method")));
}

TEST(ReflectionMembersTest, RejectsSpecialMemberFunction) {
  static_assert(!xyz::reflection_detail::is_interface_member_function(
      FindSpecialMember()));
}

TEST(ReflectionMembersTest, AcceptsOperatorFunction) {
  static_assert(
      xyz::reflection_detail::is_interface_member_function(FindOperatorPlus()));
}

TEST(ReflectionMembersTest, RejectsDataMember) {
  static_assert(!xyz::reflection_detail::is_interface_member_function(
      std::meta::nonstatic_data_members_of(
          ^^EnumerationProbe, std::meta::access_context::current())[0]));
}

struct ConstFilterProbe {
  void mutating() {}

  void observing() const {}
};

consteval std::size_t CountMembers(bool const_only) {
  return xyz::reflection_detail::interface_member_functions(^^ConstFilterProbe,
                                                            const_only)
      .size();
}

TEST(ReflectionMembersTest, InterfaceMemberFunctionsFiltersByConstness) {
  static_assert(CountMembers(false) == 2);
  static_assert(CountMembers(true) == 1);
}

struct DeclarationOrderProbe {
  void third() {}

  void first() {}

  void second() {}
};

consteval bool PreservesDeclarationOrder() {
  auto members = xyz::reflection_detail::interface_member_functions(
      ^^DeclarationOrderProbe);
  return members.size() == 3 &&
         std::meta::identifier_of(members[0]) == "third" &&
         std::meta::identifier_of(members[1]) == "first" &&
         std::meta::identifier_of(members[2]) == "second";
}

TEST(ReflectionMembersTest, InterfaceMemberFunctionsPreservesDeclarationOrder) {
  static_assert(PreservesDeclarationOrder());
}

struct CleanInterface {
  void foo() {}
};

struct ReservedSwapInterface {
  void swap(int) {}
};

struct ReservedValuelessInterface {
  void valueless_after_move() {}
};

TEST(ReflectionMembersTest, HasReservedInterfaceMemberName) {
  static_assert(!xyz::reflection_detail::has_reserved_interface_member_name(
      ^^CleanInterface));
  static_assert(xyz::reflection_detail::has_reserved_interface_member_name(
      ^^ReservedSwapInterface));
  static_assert(xyz::reflection_detail::has_reserved_interface_member_name(
      ^^ReservedValuelessInterface));
}

struct RvalueQualifiedInterface {
  void consume() && {}
};

TEST(ReflectionMembersTest, HasRvalueQualifiedInterfaceMember) {
  static_assert(!xyz::reflection_detail::has_rvalue_qualified_interface_member(
      ^^CleanInterface));
  static_assert(xyz::reflection_detail::has_rvalue_qualified_interface_member(
      ^^RvalueQualifiedInterface));
}

}  // namespace
