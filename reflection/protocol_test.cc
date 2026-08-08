// Tests for the C++26-reflection-based implementation of protocol and
// protocol_view.

#include "protocol.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

using xyz::reflection::conforms_to;
using xyz::reflection::protocol;
using xyz::reflection::protocol_view;

namespace {

// ---------------------------------------------------------------------------
// Special member function tests (protocol_view).
// ---------------------------------------------------------------------------

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

TEST(ReflectionProtocolViewTest,
     CheckSpecialMembersForStructWithDeletedSpecialMembers) {
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

// ---------------------------------------------------------------------------
// Special member function tests (protocol).
// ---------------------------------------------------------------------------

TEST(ReflectionProtocolTest, CheckSpecialMembers) {
  // protocol is not default-constructible but can be copied, moved, assigned
  // and move assigned if the underlying type can be.
  struct A {};

  static_assert(!std::is_default_constructible_v<protocol<A>>);
  static_assert(std::is_copy_constructible_v<protocol<A>>);
  static_assert(std::is_move_constructible_v<protocol<A>>);
  static_assert(std::is_copy_assignable_v<protocol<A>>);
  static_assert(std::is_move_assignable_v<protocol<A>>);
}

TEST(ReflectionProtocolTest,
     CheckSpecialMembersForStructWithDeletedSpecialMembers) {
  // protocol is not default-constructible and cannot be copied, moved,
  // assigned, or move assigned if the underlying type cannot be.
  struct D {
    D() = delete;
    D(const D&) = delete;
    D(D&&) = delete;
    D& operator=(const D&) = delete;
    D& operator=(D&&) = delete;
  };

  static_assert(!std::is_default_constructible_v<protocol<D>>);
  static_assert(!std::is_copy_constructible_v<protocol<D>>);
  static_assert(!std::is_move_constructible_v<protocol<D>>);
  static_assert(!std::is_copy_assignable_v<protocol<D>>);
  static_assert(!std::is_move_assignable_v<protocol<D>>);
}

// ---------------------------------------------------------------------------
// Conformance check tests.
// ---------------------------------------------------------------------------

TEST(ConformsToTest, EmptyInterfaceIsAlwaysSatisfied) {
  struct EmptyInterface {};
  struct Concrete {};

  static_assert(conforms_to<EmptyInterface, Concrete>());
}

TEST(ConformsToTest, ConcreteTypeConformsWhenAllMethodsMatch) {
  struct Interface {
    std::string_view name() const noexcept;
    int count();
  };
  struct Concrete {
    std::string_view name() const noexcept { return "test"; }
    int count() { return 42; }
  };

  static_assert(conforms_to<Interface, Concrete>());
}

TEST(ConformsToTest, ConcreteTypeConformsWithExtraMethodsPresent) {
  struct Interface {
    void process();
  };
  struct ConcreteWithExtra {
    void process() {}
    void extra_method() {}
    int another() const { return 0; }
  };

  static_assert(conforms_to<Interface, ConcreteWithExtra>());
}

TEST(ConformsToTest, ConcreteTypeMissingMethodDoesNotConform) {
  struct Interface {
    void foo();
    void bar();
  };
  struct MissingBar {
    void foo() {}
  };

  static_assert(!conforms_to<Interface, MissingBar>());
}

TEST(ConformsToTest, WrongConstnessDoesNotConform) {
  struct Interface {
    int value() const;
  };
  struct NonConst {
    int value();  // not const — does not match the interface
  };

  static_assert(!conforms_to<Interface, NonConst>());
}

TEST(ConformsToTest, ConstMethodNotSatisfiedByNonConstDoesNotConform) {
  struct Interface {
    void process() const;
  };
  struct NonConstProcess {
    void process() {}  // missing const qualifier
  };

  static_assert(!conforms_to<Interface, NonConstProcess>());
}

TEST(ConformsToTest, WrongReturnTypeDoesNotConform) {
  struct Interface {
    int compute();
  };
  struct WrongReturn {
    double compute() { return 0.0; }
  };

  static_assert(!conforms_to<Interface, WrongReturn>());
}

TEST(ConformsToTest, WrongParameterTypeDoesNotConform) {
  struct Interface {
    void process(int value);
  };
  struct WrongParam {
    void process(double value) {}
  };

  static_assert(!conforms_to<Interface, WrongParam>());
}

TEST(ConformsToTest, WrongParameterCountDoesNotConform) {
  struct Interface {
    void process(int a, int b);
  };
  struct WrongArity {
    void process(int a) {}
  };

  static_assert(!conforms_to<Interface, WrongArity>());
}

TEST(ConformsToTest, MultipleParametersMatchCorrectly) {
  struct Interface {
    void write(int length, double value);
  };
  struct Concrete {
    void write(int length, double value) {}
  };

  static_assert(conforms_to<Interface, Concrete>());
}

TEST(ConformsToTest, ConcreteTypeConformsForTypicalInterfaceA) {
  struct InterfaceA {
    std::string_view name() const noexcept;
    int count();
  };
  struct ConcreteA {
    std::string_view name() const noexcept { return "concrete"; }
    int count() { return 1; }
  };

  static_assert(conforms_to<InterfaceA, ConcreteA>());
}

TEST(ConformsToTest, ConcreteTypeConformsForTypicalInterfaceB) {
  struct InterfaceB {
    void process(const std::string& input);
    std::vector<int> get_results() const;
    bool is_ready() const;
  };
  struct ConcreteB {
    void process(const std::string& input) {}
    std::vector<int> get_results() const { return {}; }
    bool is_ready() const { return true; }
  };

  static_assert(conforms_to<InterfaceB, ConcreteB>());
}

}  // namespace
