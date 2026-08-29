// Tests for the C++26-reflection-based implementation of protocol and
// protocol_view.

#include "protocol.hh"

#include <gtest/gtest.h>

#include <concepts>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using xyz::reflection::is_protocol_conformant;
using xyz::reflection::is_protocol_v;
using xyz::reflection::is_protocol_view_v;
using xyz::reflection::protocol;
using xyz::reflection::protocol_view;

namespace {

// Concepts for negative member function tests: a requires-expression naming a
// member that does not exist is only a substitution failure in a template.
template <typename P>
concept has_update = requires(P& p) { p.update(0); };

template <typename P>
concept has_get_value = requires(P& p) { p.get_value(); };

// ---------------------------------------------------------------------------
// Type trait tests.
// ---------------------------------------------------------------------------

TEST(ReflectionProtocolTest, IsProtocolV) {
  struct Interface {};

  static_assert(is_protocol_v<protocol<Interface>>);
  static_assert(!is_protocol_v<protocol_view<Interface>>);
  static_assert(!is_protocol_v<Interface>);
}

TEST(ReflectionProtocolViewTest, IsProtocolViewV) {
  struct Interface {};

  static_assert(is_protocol_view_v<protocol_view<Interface>>);
  static_assert(is_protocol_view_v<protocol_view<const Interface>>);
  static_assert(!is_protocol_view_v<protocol<Interface>>);
  static_assert(!is_protocol_view_v<Interface>);
}

// ---------------------------------------------------------------------------
// Special member function tests.
// ---------------------------------------------------------------------------

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

  static_assert(!std::is_default_constructible_v<protocol_view<const A>>);
  static_assert(std::is_copy_constructible_v<protocol_view<const A>>);
  static_assert(std::is_move_constructible_v<protocol_view<const A>>);
  static_assert(std::is_copy_assignable_v<protocol_view<const A>>);
  static_assert(std::is_move_assignable_v<protocol_view<const A>>);
  static_assert(std::is_destructible_v<protocol_view<const A>>);
}

TEST(ReflectionProtocolViewTest,
     CheckSpecialMembersForStructWithDeletedSpecialMembers) {
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

TEST(ReflectionProtocolTest, CheckSpecialMembers) {
  // `protocol` is not default-constructible but can be copied, moved,
  // assigned and move assigned if the underlying type can be copied.
  struct A {};

  static_assert(!std::is_default_constructible_v<protocol<A>>);
  static_assert(std::is_copy_constructible_v<protocol<A>>);
  static_assert(std::is_move_constructible_v<protocol<A>>);
  static_assert(std::is_copy_assignable_v<protocol<A>>);
  static_assert(std::is_move_assignable_v<protocol<A>>);
}

TEST(ReflectionProtocolTest,
     CheckSpecialMembersForStructWithDeletedSpecialMembers) {
  // `protocol` is not default-constructible and cannot be copied, or
  // assigned to if the interface type cannot be copied.
  // `protocol` can be unconditionally move constructed and move assigned.
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

// ---------------------------------------------------------------------------
// Conformance check tests.
// ---------------------------------------------------------------------------

TEST(ConformsToTest, EmptyInterfaceIsAlwaysSatisfied) {
  struct EmptyInterface {};

  struct Candidate {};

  static_assert(is_protocol_conformant<EmptyInterface, Candidate>());
}

TEST(ConformsToTest, CandidateTypeConformsWhenAllMethodsMatch) {
  struct Interface {
    std::string_view name() const noexcept;
    int count();
  };

  struct Candidate {
    std::string_view name() const noexcept;
    int count();
  };

  static_assert(is_protocol_conformant<Interface, Candidate>());
}

TEST(ConformsToTest, CandidateTypeConformsWithExtraMethodsPresent) {
  struct Interface {
    void process();
  };

  struct CandidateWithExtra {
    void process();
    void extra_method();
    int another() const;
  };

  static_assert(is_protocol_conformant<Interface, CandidateWithExtra>());
}

TEST(ConformsToTest, CandidateTypeMissingMethodDoesNotConform) {
  struct Interface {
    void foo();
    void bar();
  };

  struct MissingBar {
    void foo();
  };

  static_assert(!is_protocol_conformant<Interface, MissingBar>());
}

TEST(ConformsToTest, WrongConstnessDoesNotConform) {
  struct Interface {
    int value() const;
  };

  struct NonConst {
    int value();  // not const — does not match the interface
  };

  static_assert(!is_protocol_conformant<Interface, NonConst>());
}

TEST(ConformsToTest, WrongReturnTypeDoesNotConform) {
  struct Interface {
    int compute();
  };

  struct WrongReturn {
    double compute();
  };

  static_assert(!is_protocol_conformant<Interface, WrongReturn>());
}

TEST(ConformsToTest, WrongParameterTypeDoesNotConform) {
  struct Interface {
    void process(int value);
  };

  struct WrongParam {
    void process(double value);
  };

  static_assert(!is_protocol_conformant<Interface, WrongParam>());
}

TEST(ConformsToTest, WrongParameterCountDoesNotConform) {
  struct Interface {
    void process(int a, int b);
  };

  struct WrongArity {
    void process(int a);
  };

  static_assert(!is_protocol_conformant<Interface, WrongArity>());
}

TEST(ConformsToTest, MultipleParametersMatchCorrectly) {
  struct Interface {
    void write(int length, double value);
  };

  struct Candidate {
    void write(int length, double value);
  };

  static_assert(is_protocol_conformant<Interface, Candidate>());
}

TEST(ConformsToTest, CandidateTypeConformsForTypicalInterfaceB) {
  struct InterfaceB {
    void process(const std::string& input);
    std::vector<int> get_results() const;
    bool is_ready() const;
  };

  struct CandidateB {
    void process(const std::string& input);
    std::vector<int> get_results() const;
    bool is_ready() const;
  };

  static_assert(is_protocol_conformant<InterfaceB, CandidateB>());
}

TEST(ConformsToTest, NoexceptInterfaceRequiresNoexceptCandidate) {
  struct Interface {
    void f() noexcept;
  };

  struct Conforming {
    void f() noexcept;
  };

  struct NonNoexcept {
    void f();
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
  static_assert(!is_protocol_conformant<Interface, NonNoexcept>());
}

TEST(ConformsToTest, NonNoexceptInterfaceAcceptsNoexceptCandidate) {
  struct Interface {
    void f();
  };

  struct NoexceptCandidate {
    void f() noexcept;
  };

  static_assert(is_protocol_conformant<Interface, NoexceptCandidate>());
}

TEST(ConformsToTest, LvalueRefQualifierMustMatch) {
  struct Interface {
    void f() &;
  };

  struct Conforming {
    void f() &;
  };

  struct UnqualifiedCandidate {
    void f();
  };

  struct RvalueRefCandidate {
    void f() &&;
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
  static_assert(!is_protocol_conformant<Interface, UnqualifiedCandidate>());
  static_assert(!is_protocol_conformant<Interface, RvalueRefCandidate>());
}

TEST(ConformsToTest, RvalueRefQualifierMustMatch) {
  struct Interface {
    void f() &&;
  };

  struct Conforming {
    void f() &&;
  };

  struct UnqualifiedCandidate {
    void f();
  };

  struct LvalueRefCandidate {
    void f() &;
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
  static_assert(!is_protocol_conformant<Interface, UnqualifiedCandidate>());
  static_assert(!is_protocol_conformant<Interface, LvalueRefCandidate>());
}

TEST(ConformsToTest, UnqualifiedInterfaceDoesNotMatchRefQualifiedCandidate) {
  struct Interface {
    void f();
  };

  struct LvalueRefCandidate {
    void f() &;
  };

  struct RvalueRefCandidate {
    void f() &&;
  };

  static_assert(!is_protocol_conformant<Interface, LvalueRefCandidate>());
  static_assert(!is_protocol_conformant<Interface, RvalueRefCandidate>());
}

// ---------------------------------------------------------------------------
// Constructability tests.
// ---------------------------------------------------------------------------

TEST(ReflectionProtocolTest, IsConstructibleFromConformingType) {
  struct Interface {
    std::string_view name() const noexcept;
  };

  struct Conforming {
    std::string_view name() const noexcept;
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
    std::string_view name() const noexcept;
  };

  struct NonConforming {};

  // protocol_view's constructor takes U&, so constructibility is checked
  // from an lvalue, not a prvalue.
  static_assert(std::is_constructible_v<protocol_view<Interface>, Conforming&>);
  static_assert(
      !std::is_constructible_v<protocol_view<Interface>, NonConforming&>);
}

TEST(ReflectionProtocolViewTest, NotConstructibleFromConstObject) {
  // protocol_view rejects a const object unconditionally: even though
  // Interface has no non-const methods (so a const object would actually
  // be safe to dispatch through), construction from one is still rejected,
  // because the rule doesn't inspect Interface at all.
  struct Interface {
    int get() const;
  };

  struct Conforming {
    int get() const { return 0; }
  };

  static_assert(std::is_constructible_v<protocol_view<Interface>, Conforming&>);
  static_assert(
      !std::is_constructible_v<protocol_view<Interface>, const Conforming&>);
}

TEST(ReflectionProtocolViewTest, ConstViewIsConstructibleFromConstObject) {
  struct Interface {
    std::string_view name() const noexcept;
  };

  struct Conforming {
    std::string_view name() const noexcept;
  };

  struct NonConforming {};

  static_assert(std::is_constructible_v<protocol_view<const Interface>,
                                        const Conforming&>);
  static_assert(
      std::is_constructible_v<protocol_view<const Interface>, Conforming&>);
  static_assert(!std::is_constructible_v<protocol_view<const Interface>,
                                         const NonConforming&>);
}

TEST(ReflectionProtocolTest, IsConstructibleInPlaceFromConformingType) {
  struct Interface {
    std::string_view name() const noexcept;
  };

  struct Conforming {
    std::string_view name() const noexcept;
  };

  struct NonConforming {};

  static_assert(std::is_constructible_v<protocol<Interface>,
                                        std::in_place_type_t<Conforming>>);
  static_assert(!std::is_constructible_v<protocol<Interface>,
                                         std::in_place_type_t<NonConforming>>);
}

TEST(ReflectionProtocolTest, IsConstructibleInPlaceWithArguments) {
  struct Interface {
    std::string_view name() const noexcept;
  };

  struct Conforming {
    explicit Conforming(std::string_view value);
    std::string_view name() const noexcept;
  };

  static_assert(std::is_constructible_v<protocol<Interface>,
                                        std::in_place_type_t<Conforming>,
                                        std::string_view>);
}

// Member function signature tests for protocol_view.

TEST(ReflectionProtocolViewTest, ConstMemberFunction) {
  struct Interface {
    int get_value() const;
  };

  struct Conforming {
    int get_value() const { return 42; }
  };

  Conforming c;
  protocol_view<Interface> p(c);
  EXPECT_EQ(p.get_value(), 42);
}

TEST(ReflectionProtocolViewTest, NonConstMemberFunctionInvocableFromConstView) {
  struct Interface {
    void update(int value);
  };

  // `protocol_view` has shallow const: a const view still exposes the
  // non-const member functions of the interface.
  static_assert(requires(const protocol_view<Interface>& p) {
    { p.update(0) } -> std::same_as<void>;
  });
  static_assert(has_update<const protocol_view<Interface>>);
}

TEST(ReflectionProtocolViewTest, ConstViewExposesOnlyConstMemberFunctions) {
  struct Interface {
    int get_value() const;
    void update(int value);
  };

  static_assert(has_get_value<protocol_view<const Interface>>);
  static_assert(has_get_value<const protocol_view<const Interface>>);
  static_assert(!has_update<protocol_view<const Interface>>);
  static_assert(!has_update<const protocol_view<const Interface>>);

  static_assert(requires(const protocol_view<const Interface>& p) {
    { p.get_value() } -> std::same_as<int>;
  });
}

TEST(ReflectionProtocolViewTest, NonConstMemberFunctionCalledThroughConstView) {
  struct Interface {
    void update(int value);
  };

  struct Conforming {
    int last_value = 0;

    void update(int value) { last_value = value; }
  };

  Conforming c;
  const protocol_view<Interface> p(c);
  p.update(42);
  EXPECT_EQ(c.last_value, 42);
}

TEST(ReflectionProtocolViewTest, ConstViewCallsConstMemberFunction) {
  struct Interface {
    int get_value() const;
    void update(int value);
  };

  struct Conforming {
    int value = 7;

    int get_value() const { return value; }

    void update(int new_value) { value = new_value; }
  };

  const Conforming const_object;
  protocol_view<const Interface> view_of_const(const_object);
  EXPECT_EQ(view_of_const.get_value(), 7);

  Conforming mutable_object;
  mutable_object.update(9);
  const protocol_view<const Interface> view_of_mutable(mutable_object);
  EXPECT_EQ(view_of_mutable.get_value(), 9);
}

TEST(ReflectionProtocolViewTest, ConstViewOfInterfaceWithNoConstMembers) {
  struct Interface {
    void update(int value);
  };

  // A const view of an interface with no const member functions is
  // well-formed but exposes nothing.
  static_assert(!has_update<protocol_view<const Interface>>);
  static_assert(std::is_copy_constructible_v<protocol_view<const Interface>>);
}

TEST(ReflectionProtocolViewTest, SingleParameterMemberFunction) {
  struct Interface {
    void update(int value);
  };

  struct Conforming {
    int last_value = 0;

    void update(int value) { last_value = value; }
  };

  Conforming c;
  protocol_view<Interface> p(c);
  p.update(42);
  EXPECT_EQ(c.last_value, 42);
  static_assert(!noexcept(c.update(1.0)));
}

TEST(ReflectionProtocolViewTest, NoexceptMemberFunction) {
  struct Interface {
    double compute(double input) noexcept;
  };

  struct Conforming {
    double compute(double input) noexcept { return input * 2.0; }
  };

  Conforming c;
  protocol_view<Interface> p(c);
  EXPECT_EQ(p.compute(21.0), 42.0);
  static_assert(noexcept(p.compute(1.0)));
}

TEST(ReflectionProtocolViewTest, MultiParameterMemberFunction) {
  struct Interface {
    int add(int a, int b) const;
  };

  struct Conforming {
    int add(int a, int b) const { return a + b; }
  };

  Conforming c;
  protocol_view<Interface> p(c);
  EXPECT_EQ(p.add(1, 2), 3);
}

TEST(ReflectionProtocolViewTest, VoidMemberFunction) {
  struct Interface {
    void reset();
  };

  struct Conforming {
    bool was_reset = false;

    void reset() { was_reset = true; }
  };

  Conforming c;
  protocol_view<Interface> p(c);
  p.reset();
  EXPECT_TRUE(c.was_reset);
}

TEST(ReflectionProtocolViewTest, MultipleMemberFunctions) {
  struct Interface {
    double add(double x, double y) const noexcept;
    double multiply(double x, double y) const noexcept;
  };

  struct Conforming {
    double add(double x, double y) const noexcept { return x + y; }

    double multiply(double x, double y) const noexcept { return x * y; }
  };

  Conforming c;
  protocol_view<Interface> p(c);
  EXPECT_EQ(p.add(1.0, 2.0), 3.0);
  EXPECT_EQ(p.multiply(3.0, 4.0), 12.0);
}

TEST(ReflectionProtocolViewTest, MixedConstAndMutatingMemberFunctions) {
  struct Interface {
    int get() const;
    void set(int value);
  };

  struct Conforming {
    int value = 0;

    int get() const { return value; }

    void set(int new_value) { value = new_value; }
  };

  Conforming c;
  protocol_view<Interface> p(c);
  EXPECT_EQ(p.get(), 0);
  p.set(7);
  EXPECT_EQ(p.get(), 7);
  EXPECT_EQ(c.value, 7);
}

// Member function signature tests for protocol.

TEST(ReflectionProtocolTest, ConstMemberFunction) {
  struct Interface {
    int get_value() const;
  };

  // TODO(jbcoe): replace static assertion with runtime test.
  static_assert(requires(const protocol<Interface>& p) {
    { p.get_value() } -> std::same_as<int>;
  });
}

TEST(ReflectionProtocolTest, NonConstMemberFunctionNotInvocableFromConst) {
  struct Interface {
    void update(int value);
  };

  // `protocol` propagates const: a const protocol exposes only the const
  // member functions of the interface.
  static_assert(
      !std::is_invocable_v<
          decltype((std::declval<const protocol<Interface>&>().update)), int>);
  static_assert(has_update<protocol<Interface>>);
  static_assert(!has_update<const protocol<Interface>>);
}

TEST(ReflectionProtocolTest, SingleParameterMemberFunction) {
  struct Interface {
    void update(int value);
  };

  // TODO(jbcoe): replace static assertion with runtime test.
  static_assert(requires(protocol<Interface>& p) {
    { p.update(0) } -> std::same_as<void>;
  });
}

TEST(ReflectionProtocolTest, NoexceptMemberFunction) {
  struct Interface {
    double compute(double input) noexcept;
  };

  // TODO(jbcoe): replace static assertion with runtime test.
  static_assert(requires(protocol<Interface>& p) {
    { p.compute(0.0) } noexcept -> std::same_as<double>;
  });
}

TEST(ReflectionProtocolTest, MultiParameterMemberFunction) {
  struct Interface {
    int add(int a, int b) const;
  };

  // TODO(jbcoe): replace static assertion with runtime test.
  static_assert(requires(const protocol<Interface>& p) {
    { p.add(1, 2) } -> std::same_as<int>;
  });
}

TEST(ReflectionProtocolTest, VoidMemberFunction) {
  struct Interface {
    void reset();
  };

  // TODO(jbcoe): replace static assertion with runtime test.
  static_assert(requires(protocol<Interface>& p) {
    { p.reset() } -> std::same_as<void>;
  });
}

TEST(ReflectionProtocolTest, MultipleMemberFunctions) {
  struct Interface {
    double add(double x, double y) const noexcept;
    double multiply(double x, double y) const noexcept;
  };

  // TODO(jbcoe): replace static assertion with runtime test.
  static_assert(requires(const protocol<Interface>& p) {
    { p.add(1.0, 2.0) } noexcept -> std::same_as<double>;
    { p.multiply(3.0, 4.0) } noexcept -> std::same_as<double>;
  });
}
}  // namespace
