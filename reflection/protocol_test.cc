// Tests for the C++26-reflection-based implementation of protocol and
// protocol_view.

#include "protocol.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

using xyz::reflection::is_protocol_conformant;
using xyz::reflection::is_protocol_v;
using xyz::reflection::is_protocol_view_v;
using xyz::reflection::protocol;
using xyz::reflection::protocol_view;

namespace {

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

  static_assert(std::is_constructible_v<protocol_view<Interface>, Conforming>);
  static_assert(
      !std::is_constructible_v<protocol_view<Interface>, NonConforming>);
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

}  // namespace
