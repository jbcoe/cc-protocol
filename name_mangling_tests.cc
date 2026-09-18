// Tests for the Itanium-ABI-style member function name mangling used to name
// vtable entries.

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <meta>
#include <stdexcept>

#include "name_mangling.hh"

using xyz::name_mangling::mangle;

namespace {

enum class Kind : std::uint8_t { one, two };

template <int N>
struct Tagged {};

template <long long N>
struct Tagged64 {};

struct Widget {};

struct Interface {
  int get() const;
  void update(int value);
  int operator()(int value) const;
  int cl(int value) const;
  explicit operator bool() const;
  int cv(int value) const;
  void set_int(int value);
  void set_double(double value);
  void set_widget_ref(const Widget& value);
  void set_ptr(int* value);
  void set_kind(Kind value);
  void set_two(int value, int other);
  void set_array(std::array<int, 3> value);
  void set_tagged(Tagged<3> value);
  void set_negative_tagged(Tagged<-3> value);
  void set_zero(Tagged64<0LL> value);
  void set_big(Tagged64<4294967296LL> value);
  void ref_lvalue() &;
  void ref_rvalue() &&;
};

struct Other {
  int get() const;
};

TEST(NameManglingTest, NamesAConstMemberFunctionWithNoParameters) {
  static_assert(mangle(^^Interface::get) == "fn_NK3getEi");
}

TEST(NameManglingTest, NamesAMemberFunctionWithAFundamentalParameter) {
  static_assert(mangle(^^Interface::update) == "fn_6updatevi");
  static_assert(mangle(^^Interface::set_int) == "fn_7set_intvi");
  static_assert(mangle(^^Interface::set_double) == "fn_10set_doublevd");
}

TEST(NameManglingTest, NamesTheCallOperatorUsingItsOperatorNameCode) {
  static_assert(mangle(^^Interface::operator()) == "fn_NKclEii");
}

TEST(NameManglingTest, CallOperatorDoesNotCollideWithAMemberLiterallyNamedCl) {
  static_assert(mangle(^^Interface::cl) == "fn_NK2clEii");
  static_assert(mangle(^^Interface::operator()) != mangle(^^Interface::cl));
}

TEST(NameManglingTest, NamesAConversionFunctionUsingItsTargetType) {
  static_assert(mangle(^^Interface::operator bool) == "fn_NKcvbE");
}

TEST(NameManglingTest, ConversionFunctionManglesItsTargetTypeOnce) {
  struct Conversions {
    operator Widget() const;
    operator int() noexcept;
  };

  static_assert(mangle(^^Conversions::operator Widget) ==
                "fn_NKcvN12_GLOBAL__N_16WidgetEE");
  static_assert(mangle(^^Conversions::operator int) == "fn_cviDo");
}

TEST(NameManglingTest, DistinguishesConversionFunctionsByTargetType) {
  struct IntInterface {
    operator int() const;
  };

  struct LongInterface {
    operator long() const;
  };

  static_assert(mangle(^^IntInterface::operator int) !=
                mangle(^^LongInterface::operator long));
}

TEST(NameManglingTest,
     ConversionFunctionDoesNotCollideWithAMemberLiterallyNamedCv) {
  static_assert(mangle(^^Interface::cv) == "fn_NK2cvEii");
  static_assert(mangle(^^Interface::operator bool) != mangle(^^Interface::cv));
}

TEST(NameManglingTest, NamesAPointerParameter) {
  static_assert(mangle(^^Interface::set_ptr) == "fn_7set_ptrvPi");
}

TEST(NameManglingTest, NamesAnExtendedFundamentalTypeParameter) {
  struct Interface128 {
    void set_wide(__int128 value);
    void set_wide_unsigned(unsigned __int128 value);
  };

  static_assert(mangle(^^Interface128::set_wide) == "fn_8set_widevn");
  static_assert(mangle(^^Interface128::set_wide_unsigned) !=
                mangle(^^Interface128::set_wide));
}

TEST(NameManglingTest, NamesAReferenceToALocalClassType) {
  static_assert(mangle(^^Interface::set_widget_ref) ==
                "fn_14set_widget_refvRKN12_GLOBAL__N_16WidgetE");
}

TEST(NameManglingTest, NamesAnEnumerationParameter) {
  static_assert(mangle(^^Interface::set_kind) ==
                "fn_8set_kindvN12_GLOBAL__N_14KindE");
}

TEST(NameManglingTest, NamesAClassTemplateSpecialisationParameter) {
// The expected spelling is libstdc++'s: libc++ wraps std::array in the
// inline namespace __1, which mangles differently.
#ifdef __GLIBCXX__
  static_assert(mangle(^^Interface::set_array) ==
                "fn_9set_arrayvN3std5arrayIiLm3EEE");
#endif  // __GLIBCXX__
}

TEST(NameManglingTest, NamesAnIntegralNonTypeTemplateArgument) {
  static_assert(mangle(^^Interface::set_tagged) ==
                "fn_10set_taggedvN12_GLOBAL__N_16TaggedILi3EEE");
}

TEST(NameManglingTest, NamesANegativeIntegralNonTypeTemplateArgument) {
  static_assert(mangle(^^Interface::set_negative_tagged) ==
                "fn_19set_negative_taggedvN12_GLOBAL__N_16TaggedILin3EEE");
}

TEST(NameManglingTest, DoesNotTruncateA64BitNonTypeTemplateArgumentToSizeT) {
  static_assert(mangle(^^Interface::set_zero) ==
                "fn_8set_zerovN12_GLOBAL__N_18Tagged64ILx0EEE");
  static_assert(mangle(^^Interface::set_big) ==
                "fn_7set_bigvN12_GLOBAL__N_18Tagged64ILx4294967296EEE");
  static_assert(mangle(^^Interface::set_zero) != mangle(^^Interface::set_big));
}

TEST(NameManglingTest, DistinguishesOverloadsByParameterCount) {
  static_assert(mangle(^^Interface::set_int) != mangle(^^Interface::set_two));
  static_assert(mangle(^^Interface::set_two) == "fn_7set_twovii");
}

TEST(NameManglingTest, FoldsRefQualifiersIntoTheName) {
  static_assert(mangle(^^Interface::ref_lvalue) == "fn_NR10ref_lvalueEv");
  static_assert(mangle(^^Interface::ref_rvalue) == "fn_NO10ref_rvalueEv");
  static_assert(mangle(^^Interface::ref_lvalue) !=
                mangle(^^Interface::ref_rvalue));
}

TEST(NameManglingTest, DependsOnlyOnTheSignatureNotTheEnclosingType) {
  static_assert(mangle(^^Interface::get) == mangle(^^Other::get));
}

TEST(NameManglingTest, DistinguishesMembersDifferingOnlyByReturnType) {
  struct IntInterface {
    int get() const;
  };

  struct LongInterface {
    long get() const;
  };

  static_assert(mangle(^^IntInterface::get) != mangle(^^LongInterface::get));
}

TEST(NameManglingTest, DistinguishesMembersDifferingOnlyByNoexcept) {
  struct ThrowingInterface {
    int get() const;
  };

  struct NoexceptInterface {
    int get() const noexcept;
  };

  static_assert(mangle(^^NoexceptInterface::get) == "fn_NK3getEDoi");
  static_assert(mangle(^^ThrowingInterface::get) !=
                mangle(^^NoexceptInterface::get));
}

TEST(NameManglingTest, ExtraOperators) {
  struct A {
    int operator[](int);
    void* operator*();
    void* operator->();
  };

  static_assert(mangle(^^A::operator[]) == "fn_ixii");
  static_assert(mangle(^^A::operator*) == "fn_dePv");
  static_assert(mangle(^^A::operator->) == "fn_ptPv");
}

// A member function's mangled Itanium <operator-name> for every operator
// `std::meta::operators` names, verified against the symbol names GCC
// itself emits for real, defined member operators of the same shapes.
// NOLINTBEGIN(cppcoreguidelines-special-member-functions,
// hicpp-special-member-functions, google-runtime-operator): declarations
// only, reflected but never instantiated or called.
struct AllOperators {
  // `+`, `-`, `*` and `&` mangle differently as unary (no declared
  // parameters, only the implicit object parameter) than as binary.
  int operator+();
  int operator+(int);
  int operator-();
  int operator-(int);
  int operator*();
  int operator*(int);
  int operator&();
  int operator&(int);

  // Binary-only arithmetic and bitwise operators.
  int operator/(int);
  int operator%(int);
  int operator^(int);
  int operator|(int);

  // Unary-only operators.
  int operator~();
  int operator!();

  // Assignment and compound assignment. Declaring exactly the
  // copy-assignment form, rather than some other parameter type, avoids an
  // implicitly-declared move assignment operator also existing and making
  // `^^AllOperators::operator=` ambiguous between the two.
  AllOperators& operator=(const AllOperators&);
  int operator+=(int);
  int operator-=(int);
  int operator*=(int);
  int operator/=(int);
  int operator%=(int);
  int operator^=(int);
  int operator&=(int);
  int operator|=(int);

  // Comparison.
  bool operator==(int) const;
  bool operator!=(int) const;
  bool operator<(int) const;
  bool operator>(int) const;
  bool operator<=(int) const;
  bool operator>=(int) const;
  int operator<=>(int) const;

  // Logical.
  bool operator&&(int) const;
  bool operator||(int) const;

  // Shift.
  int operator<<(int);
  int operator>>(int);
  int operator<<=(int);
  int operator>>=(int);

  // Increment/decrement: prefix (no parameters) and postfix (the dummy
  // `int` parameter C++ requires to distinguish them).
  int operator++();
  int operator++(int);
  int operator--();
  int operator--(int);

  // Comma, member access, call and subscript.
  int operator,(int);
  void* operator->();
  int operator->*(int);
  int operator()(int);
  int operator[](int);

  int operator co_await();

  // Implicitly static members: no cv/ref-qualification is possible. A
  // deallocation function with no exception-specification is implicitly
  // noexcept(true) (a C++11 defect resolution); declaring it explicitly
  // here avoids depending on whether a reflection implementation applies
  // that implicit rule.
  void* operator new(unsigned long);
  void operator delete(void*) noexcept;
  void* operator new[](unsigned long);
  void operator delete[](void*) noexcept;
};

// NOLINTEND(cppcoreguidelines-special-member-functions,
// hicpp-special-member-functions, google-runtime-operator)

// `^^AllOperators::operatorX` is ambiguous for an operator overloaded at
// more than one arity (`+`, `-`, `*`, `&`, `++`, `--`): reflecting an
// overload set is ill-formed, so the specific overload is found by arity
// instead, the same way `parameters_of` is used elsewhere in this library
// to tell a unary member operator from a binary one.
consteval std::meta::info overload_with_arity(std::meta::info type,
                                              std::meta::operators op,
                                              std::size_t arity) {
  for (std::meta::info member :
       members_of(type, std::meta::access_context::unprivileged())) {
    if (std::meta::is_function(member) && is_operator_function(member) &&
        operator_of(member) == op && parameters_of(member).size() == arity) {
      return member;
    }
  }
  throw std::runtime_error("no overload of that arity");
}

TEST(NameManglingTest,
     NamesUnaryAndBinaryFormsOfAmbiguousOperatorsDifferently) {
  using enum std::meta::operators;
  static_assert(mangle(overload_with_arity(^^AllOperators, op_plus, 0)) ==
                "fn_psi");
  static_assert(mangle(overload_with_arity(^^AllOperators, op_plus, 1)) ==
                "fn_plii");
  static_assert(mangle(overload_with_arity(^^AllOperators, op_minus, 0)) ==
                "fn_ngi");
  static_assert(mangle(overload_with_arity(^^AllOperators, op_minus, 1)) ==
                "fn_miii");
  static_assert(mangle(overload_with_arity(^^AllOperators, op_star, 0)) ==
                "fn_dei");
  static_assert(mangle(overload_with_arity(^^AllOperators, op_star, 1)) ==
                "fn_mlii");
  static_assert(mangle(overload_with_arity(^^AllOperators, op_ampersand, 0)) ==
                "fn_adi");
  static_assert(mangle(overload_with_arity(^^AllOperators, op_ampersand, 1)) ==
                "fn_anii");
}

TEST(NameManglingTest, NamesBinaryOnlyArithmeticAndBitwiseOperators) {
  static_assert(mangle(^^AllOperators::operator/) == "fn_dvii");
  static_assert(mangle(^^AllOperators::operator%) == "fn_rmii");
  static_assert(mangle(^^AllOperators::operator^) == "fn_eoii");
  static_assert(mangle(^^AllOperators::operator|) == "fn_orii");
}

TEST(NameManglingTest, NamesUnaryOnlyOperators) {
  static_assert(mangle(^^AllOperators::operator~) == "fn_coi");
  static_assert(mangle(^^AllOperators::operator!) == "fn_nti");
}

TEST(NameManglingTest, NamesAssignmentAndCompoundAssignmentOperators) {
  static_assert(mangle(^^AllOperators::operator=) ==
                "fn_aSRN12_GLOBAL__N_112AllOperatorsE"
                "RKN12_GLOBAL__N_112AllOperatorsE");
  static_assert(mangle(^^AllOperators::operator+=) == "fn_pLii");
  static_assert(mangle(^^AllOperators::operator-=) == "fn_mIii");
  static_assert(mangle(^^AllOperators::operator*=) == "fn_mLii");
  static_assert(mangle(^^AllOperators::operator/=) == "fn_dVii");
  static_assert(mangle(^^AllOperators::operator%=) == "fn_rMii");
  static_assert(mangle(^^AllOperators::operator^=) == "fn_eOii");
  static_assert(mangle(^^AllOperators::operator&=) == "fn_aNii");
  static_assert(mangle(^^AllOperators::operator|=) == "fn_oRii");
}

TEST(NameManglingTest, NamesComparisonOperators) {
  static_assert(mangle(^^AllOperators::operator==) == "fn_NKeqEbi");
  static_assert(mangle(^^AllOperators::operator!=) == "fn_NKneEbi");
  static_assert(mangle(^^AllOperators::operator<) == "fn_NKltEbi");
  static_assert(mangle(^^AllOperators::operator>) == "fn_NKgtEbi");
  static_assert(mangle(^^AllOperators::operator<=) == "fn_NKleEbi");
  static_assert(mangle(^^AllOperators::operator>=) == "fn_NKgeEbi");
  static_assert(mangle(^^AllOperators::operator<=>) == "fn_NKssEii");
}

TEST(NameManglingTest, NamesLogicalOperators) {
  static_assert(mangle(^^AllOperators::operator&&) == "fn_NKaaEbi");
  static_assert(mangle(^^AllOperators::operator||) == "fn_NKooEbi");
}

TEST(NameManglingTest, NamesShiftOperators) {
  static_assert(mangle(^^AllOperators::operator<<) == "fn_lsii");
  static_assert(mangle(^^AllOperators::operator>>) == "fn_rsii");
  static_assert(mangle(^^AllOperators::operator<<=) == "fn_lSii");
  static_assert(mangle(^^AllOperators::operator>>=) == "fn_rSii");
}

TEST(NameManglingTest, NamesIncrementAndDecrementTheSameForPrefixAndPostfix) {
  using enum std::meta::operators;
  static_assert(mangle(overload_with_arity(^^AllOperators, op_plus_plus, 0)) ==
                "fn_ppi");
  static_assert(mangle(overload_with_arity(^^AllOperators, op_plus_plus, 1)) ==
                "fn_ppii");
  static_assert(mangle(overload_with_arity(^^AllOperators, op_minus_minus,
                                           0)) == "fn_mmi");
  static_assert(mangle(overload_with_arity(^^AllOperators, op_minus_minus,
                                           1)) == "fn_mmii");
}

TEST(NameManglingTest, NamesCommaMemberAccessCallAndSubscriptOperators) {
  static_assert(mangle(^^AllOperators::operator, ) == "fn_cmii");
  static_assert(mangle(^^AllOperators::operator->) == "fn_ptPv");
  static_assert(mangle(^^AllOperators::operator->*) == "fn_pmii");
  static_assert(mangle(^^AllOperators::operator()) == "fn_clii");
  static_assert(mangle(^^AllOperators::operator[]) == "fn_ixii");
}

TEST(NameManglingTest, NamesTheCoAwaitOperator) {
  static_assert(mangle(^^AllOperators::operator co_await) == "fn_awi");
}

TEST(NameManglingTest,
     NamesAllocationOperatorsWithNoQualifiersAsTheyAreStatic) {
  static_assert(mangle(^^AllOperators::operator new) == "fn_nwPvm");
  static_assert(mangle(^^AllOperators::operator delete) == "fn_dlDovPv");
  static_assert(mangle(^^AllOperators::operator new[]) == "fn_naPvm");
  static_assert(mangle(^^AllOperators::operator delete[]) == "fn_daDovPv");
}

}  // namespace
