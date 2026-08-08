// Tests for the C++26-reflection-based implementation of protocol and
// protocol_view.
//
// Specifically covers:
//   - Special member function availability.
//   - Constructability from conforming/non-conforming types.
//   - Conformance checking via conforms_to<>.
//   - Member function stub invocability via the vanishing-this-pointer
//     synthesised members.

#include "protocol.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <type_traits>
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

// ---------------------------------------------------------------------------
// Constructability tests.
// ---------------------------------------------------------------------------

TEST(ReflectionProtocolTest, IsConstructibleFromConformingType) {
  struct Interface {
    std::string_view name() const noexcept;
  };

  struct Conforming {
    std::string_view name() const noexcept { return "conforming"; }
  };

  struct NonConforming {};

  static_assert(std::is_constructible_v<protocol<Interface>, Conforming>);
  static_assert(!std::is_constructible_v<protocol<Interface>, NonConforming>);
}

TEST(ReflectionProtocolViewTest, IsConstructibleFromConformingType) {
  struct Interface {
    std::string_view name() const noexcept;
  };

  struct Conforming {
    std::string_view name() const noexcept { return "conforming"; }
  };

  struct NonConforming {};

  static_assert(std::is_constructible_v<protocol_view<Interface>, Conforming>);
  static_assert(
      !std::is_constructible_v<protocol_view<Interface>, NonConforming>);
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

TEST(ConformsToTest, NoexceptInterfaceRequiresNoexceptConcrete) {
  struct Interface {
    void f() noexcept;
  };

  struct Conforming {
    void f() noexcept {}
  };

  struct NonNoexcept {
    void f() {}
  };

  static_assert(conforms_to<Interface, Conforming>());
  static_assert(!conforms_to<Interface, NonNoexcept>());
}

TEST(ConformsToTest, NonNoexceptInterfaceAcceptsNoexceptConcrete) {
  struct Interface {
    void f();
  };

  struct NoexceptConcrete {
    void f() noexcept {}
  };

  static_assert(conforms_to<Interface, NoexceptConcrete>());
}

TEST(ConformsToTest, LvalueRefQualifierMustMatch) {
  struct Interface {
    void f() &;
  };

  struct Conforming {
    void f() & {}
  };

  struct UnqualifiedConcrete {
    void f() {}
  };

  struct RvalueRefConcrete {
    void f() && {}
  };

  static_assert(conforms_to<Interface, Conforming>());
  static_assert(!conforms_to<Interface, UnqualifiedConcrete>());
  static_assert(!conforms_to<Interface, RvalueRefConcrete>());
}

TEST(ConformsToTest, RvalueRefQualifierMustMatch) {
  struct Interface {
    void f() &&;
  };

  struct Conforming {
    void f() && {}
  };

  struct UnqualifiedConcrete {
    void f() {}
  };

  struct LvalueRefConcrete {
    void f() & {}
  };

  static_assert(conforms_to<Interface, Conforming>());
  static_assert(!conforms_to<Interface, UnqualifiedConcrete>());
  static_assert(!conforms_to<Interface, LvalueRefConcrete>());
}

TEST(ConformsToTest, UnqualifiedInterfaceDoesNotMatchRefQualifiedConcrete) {
  struct Interface {
    void f();
  };

  struct LvalueRefConcrete {
    void f() & {}
  };

  struct RvalueRefConcrete {
    void f() && {}
  };

  static_assert(!conforms_to<Interface, LvalueRefConcrete>());
  static_assert(!conforms_to<Interface, RvalueRefConcrete>());
}

// ---------------------------------------------------------------------------
// Member function stub invocability tests.
//
// These tests verify that protocol<Interface> and protocol_view<Interface>
// expose member function stubs with the correct signatures, as synthesised
// by the vanishing-this-pointer approach in protocol.h.
//
// The stubs always call std::unreachable() internally, so the tests only
// check that the call expressions compile; they do not invoke the stubs at
// runtime (which would be undefined behaviour before the vtable layer
// exists).
// ---------------------------------------------------------------------------

// Verify that a protocol with a single const method exposes that method.
TEST(MemberStubTest, ConstMethodIsSynthesisedOnProtocol) {
  struct Interface {
    int get_value() const;
  };

  // The synthesised member must be accessible and have the right signature.
  // Use decltype to confirm the return type without executing the stub.
  static_assert(
      std::is_same_v<decltype(std::declval<const protocol<Interface>>().get_value()),
                     int>);
}

// Verify that a protocol with a non-const method exposes that method.
TEST(MemberStubTest, MutableMethodIsSynthesisedOnProtocol) {
  struct Interface {
    void update(int value);
  };

  static_assert(
      std::is_same_v<decltype(std::declval<protocol<Interface>>().update(0)),
                     void>);
}

// Verify that a noexcept method stub is marked noexcept on protocol.
TEST(MemberStubTest, NoexceptMethodStubIsNoexceptOnProtocol) {
  struct Interface {
    double compute(double input) noexcept;
  };

  static_assert(
      noexcept(std::declval<protocol<Interface>>().compute(0.0)));
}

// Verify that a const noexcept method stub is noexcept on protocol.
TEST(MemberStubTest, ConstNoexceptMethodStubIsNoexceptOnProtocol) {
  struct Interface {
    std::string_view name() const noexcept;
  };

  static_assert(
      noexcept(std::declval<const protocol<Interface>>().name()));
}

// Verify that a protocol_view exposes a const method stub.
TEST(MemberStubTest, ConstMethodIsSynthesisedOnProtocolView) {
  struct Interface {
    int get_value() const;
  };

  static_assert(
      std::is_same_v<decltype(std::declval<const protocol_view<Interface>>().get_value()),
                     int>);
}

// Verify that a protocol_view exposes a non-const method stub.
TEST(MemberStubTest, MutableMethodIsSynthesisedOnProtocolView) {
  struct Interface {
    void update(int value);
  };

  static_assert(
      std::is_same_v<decltype(std::declval<protocol_view<Interface>>().update(0)),
                     void>);
}

// Verify multi-parameter method stubs compile with the correct signature.
TEST(MemberStubTest, MultiParameterMethodIsSynthesised) {
  struct Interface {
    int add(int a, int b) const;
  };

  static_assert(
      std::is_same_v<
          decltype(std::declval<const protocol<Interface>>().add(1, 2)), int>);
}

// Verify that a method returning void is synthesised correctly.
TEST(MemberStubTest, VoidReturnMethodIsSynthesised) {
  struct Interface {
    void reset();
  };

  static_assert(
      std::is_same_v<decltype(std::declval<protocol<Interface>>().reset()),
                     void>);
}

}  // namespace
