// Tests for is_protocol_conformant.

#include <gtest/gtest.h>

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
