// Tests for the C++26-reflection-based implementation of protocol and
// protocol_view.

#include "protocol.h"

#include <gtest/gtest.h>

using xyz::reflection::protocol;
using xyz::reflection::protocol_view;

namespace {

TEST(ReflectionProtocolViewTest, CheckSpecialMembers) {
  // `protocol_view` is not default-constructible but can be copied, moved,
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
  // `protocol_view`'s special member functions do not depend on those of the
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
  // `protocol` is not default-constructible but can be copied, moved,
  // assigned and move assigned if the underlying type can be copied.
  struct A {};

  static_assert(!std::is_default_constructible_v<protocol<A>>);
  static_assert(std::is_copy_constructible_v<protocol<A>>);
  static_assert(std::is_move_constructible_v<protocol<A>>);
  static_assert(std::is_copy_assignable_v<protocol<A>>);
  static_assert(std::is_move_assignable_v<protocol<A>>);
}

TEST(RProtocolTest, CheckSpecialMembersForStructWithDeletedSpecialMembers) {
  // `protocol` is not default-constructible and cannot be copied, or
  // assigned to if the interface type cannot be copied.
  // `protocol` can unconditionally be move constructed and move assigned.
  struct D {
    D() = default;         // Ignored.
    D(const D&) = delete;  // Suppresses copy and assigment for `protocol<D>`.
    D(D&&) = default;      // Ignored.
    D& operator=(const D&) = default;  // Ignored.
    D& operator=(D&&) = default;       // Ignored.
  };

  static_assert(!std::is_default_constructible_v<protocol<D>>);
  static_assert(!std::is_copy_constructible_v<protocol<D>>);
  static_assert(std::is_move_constructible_v<protocol<D>>);
  static_assert(std::is_move_constructible_v<protocol<D>>);
  static_assert(!std::is_copy_assignable_v<protocol<D>>);
  static_assert(std::is_move_assignable_v<protocol<D>>);
}
}  // namespace
