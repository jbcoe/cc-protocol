// Tests for the C++26-reflection-based implementation of protocol and
// protocol_view.

#include "protocol.h"

#include <gtest/gtest.h>

using xyz::reflection::protocol;
using xyz::reflection::protocol_view;

namespace {

TEST(ReflectionProtocolViewTest, CheckSpecialMembers) {
  // protocol_view is not default-constructible but can be copied, moved,
  // assigned, move assigned and destroyed.
  struct A {};

  static_assert(!std::is_default_constructible_v<protocol_view<A>>);
  static_assert(std::is_copy_constructible_v<protocol_view<A>>);
  static_assert(std::is_move_constructible_v<protocol_view<A>>);
  static_assert(std::is_copy_assignable_v<protocol_view<A>>);
  static_assert(std::is_move_assignable_v<protocol_view<A>>);
  static_assert(std::is_destructible_v<protocol_view<A>>);
}

TEST(RProtocolViewTest, CheckSpecialMembersForStructWithDeletedSpecialMembers) {
  // protocol_view's special member functions do not depend on those of the
  // viewed type.
  struct D {
    D() = delete;
    D(const D&) = delete;
    D(D&&) = delete;
    D& operator=(const D&) = delete;
    D& operator=(D&&) = delete;
    ~D() = delete;
  };

  static_assert(!std::is_default_constructible_v<protocol_view<D>>);
  static_assert(std::is_copy_constructible_v<protocol_view<D>>);
  static_assert(std::is_move_constructible_v<protocol_view<D>>);
  static_assert(std::is_copy_assignable_v<protocol_view<D>>);
  static_assert(std::is_move_assignable_v<protocol_view<D>>);
  static_assert(std::is_destructible_v<protocol_view<D>>);
}

TEST(RProtocolTest, CheckSpecialMembers) {
  // protocol is default-constructible when the interface is both default- and
  // copy-constructible and the allocator is default-constructible. It can also
  // be copied, moved, assigned, and move assigned.
  struct A {};

  static_assert(std::is_default_constructible_v<protocol<A>>);
  static_assert(std::is_copy_constructible_v<protocol<A>>);
  static_assert(std::is_move_constructible_v<protocol<A>>);
  static_assert(std::is_copy_assignable_v<protocol<A>>);
  static_assert(std::is_move_assignable_v<protocol<A>>);
}

TEST(RProtocolTest, CheckSpecialMembersForStructWithDeletedSpecialMembers) {
  // protocol's default constructor depends on the interface, but its copy and
  // move support does not.
  struct D {
    D() = delete;
    D(const D&) = delete;
    D(D&&) = delete;
    D& operator=(const D&) = delete;
    D& operator=(D&&) = delete;
  };

  static_assert(!std::is_default_constructible_v<protocol<D>>);
  static_assert(!std::is_copy_constructible_v<protocol<D>>);
  static_assert(std::is_move_constructible_v<protocol<D>>);
  static_assert(!std::is_copy_assignable_v<protocol<D>>);
  static_assert(std::is_move_assignable_v<protocol<D>>);
}

TEST(RProtocolTest,
     CheckDefaultConstructionRequiresDefaultConstructibleInterface) {
  struct A {
    A() = delete;
    A(const A&) = default;
  };

  static_assert(!std::is_default_constructible_v<protocol<A>>);
}

TEST(RProtocolTest, CheckDefaultConstructionRequiresDefaultAllocator) {
  struct A {};

  struct NonDefaultAllocator {
    NonDefaultAllocator() = delete;
  };

  static_assert(
      !std::is_default_constructible_v<protocol<A, NonDefaultAllocator>>);
}
}  // namespace
