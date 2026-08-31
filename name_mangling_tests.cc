// Tests for the Itanium-ABI-style member function name mangling used to name
// vtable entries.

#include <gtest/gtest.h>

#include <array>

#include "name_mangling.h"

using xyz::name_mangling::mangle;

namespace {

enum class Kind { one, two };

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
  static_assert(mangle(^^Interface::set_array) ==
                "fn_9set_arrayvN3std5arrayIiLm3EEE");
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

}  // namespace
