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

struct Widget {};

struct Interface {
  int get() const;
  void update(int value);
  int operator()(int value) const;
  void set_int(int value);
  void set_double(double value);
  void set_widget_ref(const Widget& value);
  void set_ptr(int* value);
  void set_kind(Kind value);
  void set_two(int value, int other);
  void set_array(std::array<int, 3> value);
  void set_tagged(Tagged<3> value);
  void ref_lvalue() &;
  void ref_rvalue() &&;
};

struct Other {
  int get() const;
};

TEST(NameManglingTest, NamesAConstMemberFunctionWithNoParameters) {
  static_assert(mangle(^^Interface::get) == "fn_NK3getE");
}

TEST(NameManglingTest, NamesAMemberFunctionWithAFundamentalParameter) {
  static_assert(mangle(^^Interface::update) == "fn_6updatei");
  static_assert(mangle(^^Interface::set_int) == "fn_7set_inti");
  static_assert(mangle(^^Interface::set_double) == "fn_10set_doubled");
}

TEST(NameManglingTest, NamesTheCallOperatorUsingItsOperatorNameCode) {
  static_assert(mangle(^^Interface::operator()) == "fn_NK2clEi");
}

TEST(NameManglingTest, NamesAPointerParameter) {
  static_assert(mangle(^^Interface::set_ptr) == "fn_7set_ptrPi");
}

TEST(NameManglingTest, NamesAReferenceToALocalClassType) {
  static_assert(mangle(^^Interface::set_widget_ref) ==
                "fn_14set_widget_refRKN12_GLOBAL__N_16WidgetE");
}

TEST(NameManglingTest, NamesAnEnumerationParameter) {
  static_assert(mangle(^^Interface::set_kind) ==
                "fn_8set_kindN12_GLOBAL__N_14KindE");
}

TEST(NameManglingTest, NamesAClassTemplateSpecialisationParameter) {
  static_assert(mangle(^^Interface::set_array) ==
                "fn_9set_arrayN3std5arrayIiLm3EEE");
}

TEST(NameManglingTest, NamesAnIntegralNonTypeTemplateArgument) {
  static_assert(mangle(^^Interface::set_tagged) ==
                "fn_10set_taggedN12_GLOBAL__N_16TaggedILi3EEE");
}

TEST(NameManglingTest, DistinguishesOverloadsByParameterCount) {
  static_assert(mangle(^^Interface::set_int) != mangle(^^Interface::set_two));
  static_assert(mangle(^^Interface::set_two) == "fn_7set_twoii");
}

TEST(NameManglingTest, FoldsRefQualifiersIntoTheName) {
  static_assert(mangle(^^Interface::ref_lvalue) == "fn_NR10ref_lvalueE");
  static_assert(mangle(^^Interface::ref_rvalue) == "fn_NO10ref_rvalueE");
  static_assert(mangle(^^Interface::ref_lvalue) !=
                mangle(^^Interface::ref_rvalue));
}

TEST(NameManglingTest, DependsOnlyOnTheSignatureNotTheEnclosingType) {
  static_assert(mangle(^^Interface::get) == mangle(^^Other::get));
}

}  // namespace
