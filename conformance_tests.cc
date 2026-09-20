// Tests for is_protocol_conformant.

#include <gtest/gtest.h>

#include <algorithm>
#include <compare>
#include <meta>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "protocol.hh"

using xyz::reflection::is_protocol_conformant;

namespace {

// ---------------------------------------------------------------------------
// Conformance check tests.
// ---------------------------------------------------------------------------

// Returns `true` if checking conformance of `Candidate` against `Interface`
// throws during constant evaluation, as it does for a ref-qualified
// interface member function. Observing the rejection means catching the
// exception at constant evaluation time (P3068 constexpr exceptions), which
// GCC trunk implements but the clang-p2996 fork used for clang-tidy does
// not; the rejection itself does not depend on P3068.
#ifdef __cpp_constexpr_exceptions
template <typename Interface, typename Candidate>
consteval bool conformance_check_rejects() {
  try {
    (void)is_protocol_conformant<Interface, Candidate>();
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

// Returns `true` if checking conformance of `Candidate` against `Interface`
// throws during constant evaluation with a message that contains `text`.
// A display string is implementation-defined, so tests look for the parts
// of a member's declaration that set it apart from its neighbours.
template <typename Interface, typename Candidate>
consteval bool conformance_rejection_message_contains(std::string_view text) {
  try {
    (void)is_protocol_conformant<Interface, Candidate>();
  } catch (const std::runtime_error& error) {
    return std::string_view(error.what()).contains(text);
  }
  return false;
}
#endif  // __cpp_constexpr_exceptions

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

TEST(ConformsToTest, RefQualifiedInterfaceMembersAreRejected) {
  struct LvalueRefInterface {
    void f() &;
  };

  struct RvalueRefInterface {
    void f() &&;
  };

  struct MatchingLvalueRefCandidate {
    void f() &;
  };

  struct MatchingRvalueRefCandidate {
    void f() &&;
  };

  struct UnqualifiedCandidate {
    void f();
  };

#ifdef __cpp_constexpr_exceptions
  static_assert(conformance_check_rejects<LvalueRefInterface,
                                          MatchingLvalueRefCandidate>());
  static_assert(
      conformance_check_rejects<LvalueRefInterface, UnqualifiedCandidate>());
  static_assert(conformance_check_rejects<RvalueRefInterface,
                                          MatchingRvalueRefCandidate>());
  static_assert(
      conformance_check_rejects<RvalueRefInterface, UnqualifiedCandidate>());
#endif  // __cpp_constexpr_exceptions
}

TEST(ConformsToTest, UnsupportedOperatorsAreRejected) {
  struct EqualsInterface {
    EqualsInterface& operator=(int);
  };

  struct CoAwaitInterface {
    int operator co_await();
  };

  struct Candidate {};

#ifdef __cpp_constexpr_exceptions
  static_assert(conformance_check_rejects<EqualsInterface, Candidate>());
  static_assert(conformance_check_rejects<CoAwaitInterface, Candidate>());
#endif  // __cpp_constexpr_exceptions
}

// A local class cannot define a friend function, so the types with hidden
// friends for the tests below live at namespace scope. Nothing calls the
// friends, hence `[[maybe_unused]]`.
struct HiddenFriendCandidate {
  [[maybe_unused]] friend bool operator==(const HiddenFriendCandidate&, int) {
    return true;
  }
};

TEST(ConformsToTest, ComparisonOperatorsAreRequiredOfCandidates) {
  struct Interface {
    bool operator==(int rhs) const;
  };

  struct MemberCandidate {
    bool operator==(int rhs) const;
  };

  struct EmptyCandidate {};

  static_assert(is_protocol_conformant<Interface, MemberCandidate>());
  static_assert(!is_protocol_conformant<Interface, HiddenFriendCandidate>());
  static_assert(!is_protocol_conformant<Interface, EmptyCandidate>());

  struct SpaceshipInterface {
    std::strong_ordering operator<=>(int rhs) const;
  };

  struct MemberSpaceshipCandidate {
    std::strong_ordering operator<=>(int rhs) const;
  };

  static_assert(
      is_protocol_conformant<SpaceshipInterface, MemberSpaceshipCandidate>());
  static_assert(!is_protocol_conformant<SpaceshipInterface, EmptyCandidate>());
}

TEST(ConformsToTest, ComparisonWithInterfaceTypeOperandIsRejected) {
  struct ReferenceOperandInterface {
    bool operator==(const ReferenceOperandInterface& rhs) const;
  };

  struct ValueOperandInterface {
    std::strong_ordering operator<=>(ValueOperandInterface rhs) const;
  };

  struct Candidate {
    bool operator==(const ReferenceOperandInterface& rhs) const;
    std::strong_ordering operator<=>(ValueOperandInterface rhs) const;
  };

#ifdef __cpp_constexpr_exceptions
  static_assert(
      conformance_check_rejects<ReferenceOperandInterface, Candidate>());
  static_assert(conformance_check_rejects<ValueOperandInterface, Candidate>());
#endif  // __cpp_constexpr_exceptions
}

struct HiddenFriendComparisonInterface {
  int f() const;

  [[maybe_unused]] friend bool operator==(
      const HiddenFriendComparisonInterface&,
      const HiddenFriendComparisonInterface&) {
    return true;
  }
};

TEST(ConformsToTest, HiddenFriendComparisonOnInterfaceIsIgnored) {
  struct Candidate {
    int f() const;
  };

  static_assert(
      is_protocol_conformant<HiddenFriendComparisonInterface, Candidate>());
}

TEST(ConformsToTest, DefaultedComparisonOperatorsAreRejected) {
  struct DefaultedEqualsInterface {
    int f() const;
    bool operator==(const DefaultedEqualsInterface&) const = default;
  };

  struct DefaultedSpaceshipInterface {
    int f() const;
    auto operator<=>(const DefaultedSpaceshipInterface&) const = default;
  };

  struct Candidate {
    int f() const;
  };

#ifdef __cpp_constexpr_exceptions
  static_assert(
      conformance_check_rejects<DefaultedEqualsInterface, Candidate>());
  static_assert(
      conformance_check_rejects<DefaultedSpaceshipInterface, Candidate>());
#endif  // __cpp_constexpr_exceptions
}

TEST(ConformsToTest, UnaryAmpersandInterfaceMemberIsRejected) {
  struct UnaryAmpersandInterface {
    // NOLINTBEGIN(google-runtime-operator): the rejection is what's under
    // test.
    int* operator&();
    // NOLINTEND(google-runtime-operator)
  };

  struct BinaryAmpersandInterface {
    int operator&(int rhs);
  };

  struct Candidate {
    int operator&(int rhs) { return rhs; }
  };

#ifdef __cpp_constexpr_exceptions
  static_assert(
      conformance_check_rejects<UnaryAmpersandInterface, Candidate>());
#endif  // __cpp_constexpr_exceptions
  static_assert(is_protocol_conformant<BinaryAmpersandInterface, Candidate>());
}

TEST(ConformsToTest, MissingConversionFunctionIsRejected) {
  struct BoolConvertibleInterface {
    int f() const;
    explicit operator bool() const;
  };

  struct Candidate {
    int f() const { return 0; }
  };

  static_assert(!is_protocol_conformant<BoolConvertibleInterface, Candidate>());
}

TEST(ConformsToTest, ConversionFunctionExplicitnessMustMatch) {
  struct ExplicitInterface {
    explicit operator bool() const;
  };

  struct NonExplicitInterface {
    operator bool() const;
  };

  struct ExplicitCandidate {
    explicit operator bool() const { return true; }
  };

  struct NonExplicitCandidate {
    operator bool() const { return true; }
  };

  static_assert(is_protocol_conformant<ExplicitInterface, ExplicitCandidate>());
  static_assert(
      !is_protocol_conformant<ExplicitInterface, NonExplicitCandidate>());
  static_assert(
      is_protocol_conformant<NonExplicitInterface, NonExplicitCandidate>());
  static_assert(
      !is_protocol_conformant<NonExplicitInterface, ExplicitCandidate>());
}

// A local class can't have a member template, so this candidate for
// `TemplatedConversionFunctionsAreIgnored` lives at namespace scope.
struct CandidateWithTemplatedConversion {
  template <typename T>
  explicit operator T() const;

  explicit operator bool() const { return true; }
};

TEST(ConformsToTest, TemplatedConversionFunctionsAreIgnored) {
  struct BoolConvertibleInterface {
    explicit operator bool() const;
  };

  static_assert(is_protocol_conformant<BoolConvertibleInterface,
                                       CandidateWithTemplatedConversion>());
}

// A local class can't have a member template, so the interfaces for
// `InterfaceMemberFunctionTemplatesAreRejected` and
// `InterfaceConstructorAndStaticTemplatesAreIgnored` live at namespace scope.
struct InterfaceWithMemberFunctionTemplate {
  int f() const;

  template <typename T>
  void g(T) const;
};

struct InterfaceWithOperatorTemplate {
  int f() const;

  template <typename T>
  int operator+(T) const;
};

struct InterfaceWithConversionFunctionTemplate {
  int f() const;

  template <typename T>
  explicit operator T() const;
};

struct InterfaceWithConstructorAndStaticTemplates {
  template <typename T>
  explicit InterfaceWithConstructorAndStaticTemplates(T);

  template <typename T>
  static void make(T);

  int f() const;
};

TEST(ConformsToTest, InterfaceMemberFunctionTemplatesAreRejected) {
  struct Candidate {
    int f() const { return 0; }
  };

#ifdef __cpp_constexpr_exceptions
  static_assert(conformance_check_rejects<InterfaceWithMemberFunctionTemplate,
                                          Candidate>());
  static_assert(
      conformance_check_rejects<InterfaceWithOperatorTemplate, Candidate>());
  static_assert(
      conformance_check_rejects<InterfaceWithConversionFunctionTemplate,
                                Candidate>());
  // A candidate with the same templates is rejected too: the interface is at
  // fault, not the candidate.
  static_assert(
      conformance_check_rejects<InterfaceWithMemberFunctionTemplate,
                                InterfaceWithMemberFunctionTemplate>());
#endif  // __cpp_constexpr_exceptions
}

TEST(ConformsToTest, InterfaceConstructorAndStaticTemplatesAreIgnored) {
  struct Candidate {
    int f() const { return 0; }
  };

  static_assert(
      is_protocol_conformant<InterfaceWithConstructorAndStaticTemplates,
                             Candidate>());
}

TEST(ConformsToTest, ExplicitObjectInterfaceMembersAreRejected) {
  struct ExplicitObjectInterface {
    int f(this const ExplicitObjectInterface&);
  };

  struct Candidate {
    int f() const { return 0; }
  };

#ifdef __cpp_constexpr_exceptions
  static_assert(
      conformance_check_rejects<ExplicitObjectInterface, Candidate>());
#endif  // __cpp_constexpr_exceptions
}

struct FirstOperand {};

struct SecondOperand {};

// At namespace scope because a local class cannot declare a member template.
struct InterfaceWithNamedMemberFunctionTemplate {
  template <typename T>
  void named_template(T);
};

TEST(ConformsToTest, ReservedMemberNamesAreRejected) {
  struct SwapInterface {
    void swap(SwapInterface&);
  };

  struct GetAllocatorInterface {
    int get_allocator() const;
  };

  struct AllocatorTypeInterface {
    int allocator_type() const;
  };

  struct Candidate {
    void swap(SwapInterface&) {}

    int get_allocator() const { return 0; }

    int allocator_type() const { return 0; }
  };

#ifdef __cpp_constexpr_exceptions
  static_assert(conformance_check_rejects<SwapInterface, Candidate>());
  static_assert(conformance_check_rejects<GetAllocatorInterface, Candidate>());
  static_assert(conformance_check_rejects<AllocatorTypeInterface, Candidate>());
  static_assert(
      conformance_rejection_message_contains<GetAllocatorInterface, Candidate>(
          "reserved member name 'get_allocator'"));
#endif  // __cpp_constexpr_exceptions
}

TEST(ConformsToTest, StaticInterfaceMemberWithReservedNameIsIgnored) {
  struct Interface {
    static void swap(Interface&, Interface&);
    int f() const;
  };

  struct Candidate {
    int f() const { return 0; }
  };

  static_assert(is_protocol_conformant<Interface, Candidate>());
}

// Returns `true` if the public members of `type` that have an identifier are
// exactly those `reserved_member_names` lists.
consteval bool public_member_names_are_reserved(std::meta::info type) {
  std::vector<std::string_view> names;
  for (std::meta::info member :
       members_of(type, std::meta::access_context::unprivileged())) {
    if (has_identifier(member)) names.push_back(identifier_of(member));
  }
  return std::ranges::all_of(names,
                             [](std::string_view name) {
                               return std::ranges::contains(
                                   xyz::detail::reserved_member_names, name);
                             }) &&
         std::ranges::all_of(xyz::detail::reserved_member_names,
                             [&](std::string_view name) {
                               return std::ranges::contains(names, name);
                             });
}

// Returns `true` if `type` has no public member with an identifier.
consteval bool has_no_public_member_names(std::meta::info type) {
  return std::ranges::none_of(
      members_of(type, std::meta::access_context::unprivileged()),
      std::meta::has_identifier);
}

// A public member added to `protocol` or `protocol_view` hides an interface
// member function of the same name, so it has to be added to
// `reserved_member_names` too; prefer a hidden friend, which reserves no name.
TEST(ConformsToTest, ReservedMemberNamesAreThePublicMembersOfProtocol) {
  struct Interface {
    int f() const;
  };

  static_assert(
      public_member_names_are_reserved(^^xyz::reflection::protocol<Interface>));
  static_assert(
      has_no_public_member_names(^^xyz::reflection::protocol_view<Interface>));
  static_assert(has_no_public_member_names(
      ^^xyz::reflection::protocol_view<const Interface>));
}

TEST(ConformsToTest, RejectionMessageNamesTheMember) {
  struct RefQualifiedInterface {
    void named_function() &;
  };

  struct ExplicitObjectInterface {
    void named_function(this ExplicitObjectInterface&);
  };

  struct AddressOfInterface {
    // NOLINTBEGIN(google-runtime-operator): the rejection is what's under
    // test.
    int* operator&();
    // NOLINTEND(google-runtime-operator)
  };

  struct CoAwaitInterface {
    int operator co_await();
  };

  struct Candidate {};

#ifdef __cpp_constexpr_exceptions
  static_assert(
      conformance_rejection_message_contains<RefQualifiedInterface, Candidate>(
          "ref-qualified member function 'named_function'"));
  static_assert(conformance_rejection_message_contains<ExplicitObjectInterface,
                                                       Candidate>(
      "explicit-object member function 'named_function'"));
  static_assert(conformance_rejection_message_contains<
                InterfaceWithNamedMemberFunctionTemplate, Candidate>(
      "member function template 'named_template'"));
  static_assert(
      conformance_rejection_message_contains<AddressOfInterface, Candidate>(
          "address-of operator '"));
  static_assert(
      conformance_rejection_message_contains<AddressOfInterface, Candidate>(
          "operator&"));
  static_assert(
      conformance_rejection_message_contains<CoAwaitInterface, Candidate>(
          "operator co_await"));
#endif  // __cpp_constexpr_exceptions
}

TEST(ConformsToTest, RejectionMessageNamesTheOverload) {
  struct Interface {
    Interface& operator=(const FirstOperand&);
    Interface& operator=(const SecondOperand&);
  };

  struct Candidate {};

#ifdef __cpp_constexpr_exceptions
  // The first rejected overload is the one reported.
  static_assert(conformance_rejection_message_contains<Interface, Candidate>(
      "operator '"));
  static_assert(conformance_rejection_message_contains<Interface, Candidate>(
      "operator="));
  static_assert(conformance_rejection_message_contains<Interface, Candidate>(
      "FirstOperand"));
  static_assert(!conformance_rejection_message_contains<Interface, Candidate>(
      "SecondOperand"));
#endif  // __cpp_constexpr_exceptions
}

TEST(ConformsToTest, RejectedOperatorIsNamedTheSameWayByEveryCheck) {
  // Ref-qualified and an unsupported operator: whichever check fires, the
  // member is named by its display string.
  struct Interface {
    bool operator==(const FirstOperand&) const&;
  };

  struct Candidate {};

#ifdef __cpp_constexpr_exceptions
  static_assert(conformance_rejection_message_contains<Interface, Candidate>(
      "ref-qualified member function '"));
  static_assert(conformance_rejection_message_contains<Interface, Candidate>(
      "operator=="));
  static_assert(conformance_rejection_message_contains<Interface, Candidate>(
      "FirstOperand"));
#endif  // __cpp_constexpr_exceptions
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

TEST(ConformsToTest, OverloadedMemberFunctionsConform) {
  struct Interface {
    int compute(int value);
    double compute(double value);
    std::string compute(const std::string& value) const;
  };

  struct Conforming {
    int compute(int value) { return value; }

    double compute(double value) { return value; }

    std::string compute(const std::string& value) const { return value; }
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
}

TEST(ConformsToTest, CandidateMissingOverloadDoesNotConform) {
  struct Interface {
    int compute(int value);
    double compute(double value);
    std::string compute(const std::string& value) const;
  };

  struct MissingOverloads {
    int compute(int value) { return value; }
  };

  static_assert(!is_protocol_conformant<Interface, MissingOverloads>());
}

TEST(ConformsToTest, CandidateWithExtraOverloadsConforms) {
  struct Interface {
    int compute(int value);
  };

  struct CandidateWithExtraOverload {
    int compute(int value) { return value; }

    double compute(double value) { return value; }
  };

  static_assert(
      is_protocol_conformant<Interface, CandidateWithExtraOverload>());
}

TEST(ConformsToTest, OverloadsByArityConform) {
  struct Interface {
    int f(int a);
    int f(int a, int b);
  };

  struct Conforming {
    int f(int a) { return a; }

    int f(int a, int b) { return a + b; }
  };

  struct MissingUnaryOverload {
    int f(int a, int b) { return a + b; }
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
  static_assert(!is_protocol_conformant<Interface, MissingUnaryOverload>());
}

TEST(ConformsToTest, ConstAndNonConstOverloadPairConforms) {
  struct Interface {
    int f() const;
    int f();
  };

  struct Conforming {
    int f() const { return 1; }

    int f() { return 2; }
  };

  struct ConstOnly {
    int f() const { return 1; }
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
  static_assert(!is_protocol_conformant<Interface, ConstOnly>());
}

TEST(ConformsToTest, CallOperatorConforms) {
  struct Interface {
    int operator()(int x) const;
  };

  struct Conforming {
    int operator()(int x) const { return x + 1; }
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());

  auto lambda = [](int x) { return x + 1; };
  static_assert(is_protocol_conformant<Interface, decltype(lambda)>());
}

TEST(ConformsToTest, CallOperatorWithWrongSignatureDoesNotConform) {
  struct Interface {
    int operator()(int x) const;
  };

  struct WrongParam {
    int operator()(double x) const { return static_cast<int>(x); }
  };

  struct NonConst {
    int operator()(int x) { return x; }
  };

  static_assert(!is_protocol_conformant<Interface, WrongParam>());
  static_assert(!is_protocol_conformant<Interface, NonConst>());
}

TEST(ConformsToTest, MutableLambdaConformsToNonConstCallOperator) {
  struct Interface {
    int operator()(int x);
  };

  auto mutable_lambda = [](int x) mutable { return x + 1; };
  auto const_lambda = [](int x) { return x + 1; };

  static_assert(is_protocol_conformant<Interface, decltype(mutable_lambda)>());
  static_assert(!is_protocol_conformant<Interface, decltype(const_lambda)>());
}

TEST(ConformsToTest, OverloadedCallOperatorsConform) {
  struct Interface {
    int operator()(int x);
    double operator()(double x);
    std::string operator()(const std::string& x) const;
  };

  struct Conforming {
    int operator()(int x) { return x * 2; }

    double operator()(double x) { return x * 3.0; }

    std::string operator()(const std::string& x) const { return x + x; }
  };

  struct MissingOverloads {
    int operator()(int x) { return x * 2; }
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
  static_assert(!is_protocol_conformant<Interface, MissingOverloads>());
}

TEST(ConformsToTest, StaticCandidateConformsToConstMember) {
  struct Interface {
    int value() const;
  };

  struct Conforming {
    static int value();
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
}

TEST(ConformsToTest, StaticCandidateConformsToNonConstMember) {
  struct Interface {
    int next();
  };

  struct Conforming {
    static int next();
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
}

TEST(ConformsToTest, RefQualifiedInterfaceMemberRejectedForStaticCandidate) {
  struct Interface {
    int take() &&;
  };

  struct Conforming {
    static int take();
  };

#ifdef __cpp_constexpr_exceptions
  static_assert(conformance_check_rejects<Interface, Conforming>());
#endif  // __cpp_constexpr_exceptions
}

TEST(ConformsToTest, StaticCandidateWithWrongSignatureDoesNotConform) {
  struct Interface {
    int value(int x) const;
  };

  struct WrongParam {
    static int value(double x);
  };

  struct WrongReturn {
    static double value(int x);
  };

  static_assert(!is_protocol_conformant<Interface, WrongParam>());
  static_assert(!is_protocol_conformant<Interface, WrongReturn>());
}

TEST(ConformsToTest, NoexceptInterfaceRequiresNoexceptStaticCandidate) {
  struct Interface {
    int value() const noexcept;
  };

  struct Conforming {
    static int value() noexcept;
  };

  struct NonNoexcept {
    static int value();
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
  static_assert(!is_protocol_conformant<Interface, NonNoexcept>());
}

TEST(ConformsToTest, InterfaceStaticMembersAreIgnored) {
  struct Interface {
    static int helper();
    int value() const;
  };

  struct Conforming {
    int value() const { return 1; }
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
}

TEST(ConformsToTest, StaticCallOperatorConforms) {
  struct Interface {
    int operator()(int x) const;
  };

  struct Conforming {
    static int operator()(int x) { return x + 1; }
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
}

TEST(ConformsToTest, StaticOverloadConformsAlongsideNonStatic) {
  struct Interface {
    int f(int x) const;
    int f(double x) const;
  };

  struct Conforming {
    int f(int x) const { return x; }

    static int f(double x) { return static_cast<int>(x); }
  };

  static_assert(is_protocol_conformant<Interface, Conforming>());
}

}  // namespace
