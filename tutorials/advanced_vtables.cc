/* Copyright (c) 2025 The XYZ Protocol Authors. All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
==============================================================================*/

#include <gtest/gtest.h>

#include <array>
#include <charconv>
#include <cstddef>
#include <limits>
#include <meta>
#include <ranges>
#include <string>
#include <string_view>

#include "consteval_check.h"

// A study of using vtables for overloaded member functions and `operator()`.
// This tutorial follows on from the work in `tutorials/polymorphism.cc`.

namespace xyz::tutorials::simple_vtable {

class Cat {
 public:
  std::string_view noise() const { return "Meow"; }
};

struct vtable {
  std::string_view (*noise_func_)(const void* data);
};

class AnimalPtr {
  void* data_;
  const vtable* vtable_;

 public:
  template <typename T>
  AnimalPtr(T* t) : data_(t) {
    // Making the vtable `constexpr` avoids runtime cost (synchronization
    // between threads) when accessing static variables.
    constexpr static vtable vtable_for_type = {
        .noise_func_ =
            +[](const void* data) {
              return static_cast<const T*>(data)->noise();
            },
    };

    vtable_ = &vtable_for_type;
  }

  std::string_view noise() const { return vtable_->noise_func_(data_); }
};

TEST(TutorialsVtables, SimpleVtable) {
  Cat cat;

  AnimalPtr a(&cat);

  EXPECT_EQ(a.noise(), "Meow");
}

}  // namespace xyz::tutorials::simple_vtable

namespace xyz::tutorials::vtable_with_overload {
class Cat {
 public:
  std::string_view noise() const { return "Meow"; }

  std::string_view noise(int) const { return "Purr"; }
};

struct vtable {
  // With overloads, vtable entry names must represent type signatures.
  std::string_view (*noise_void_func_)(const void* data);
  std::string_view (*noise_int_func_)(const void* data, int);
};

class AnimalPtr {
  void* data_;
  const vtable* vtable_;

 public:
  template <typename T>
  AnimalPtr(T* t) : data_(t) {
    // Making the vtable `constexpr` avoids runtime cost (synchronization
    // between threads) when accessing static variables.
    constexpr static vtable vtable_for_type = {
        .noise_void_func_ =
            +[](const void* data) {
              return static_cast<const T*>(data)->noise();
            },
        .noise_int_func_ =
            +[](const void* data, int x) {
              return static_cast<const T*>(data)->noise(x);
            },
    };

    vtable_ = &vtable_for_type;
  }

  std::string_view noise() const {  //
    return vtable_->noise_void_func_(data_);
  }

  std::string_view noise(int x) const {
    return vtable_->noise_int_func_(data_, x);
  }
};

TEST(TutorialsVtables, VtableWithOverloads) {
  Cat cat;

  AnimalPtr a(&cat);

  EXPECT_EQ(a.noise(), "Meow");
  EXPECT_EQ(a.noise(0), "Purr");
}
}  // namespace xyz::tutorials::vtable_with_overload

namespace xyz::tutorials::vtable_with_operators {
struct Cat {
  std::string_view operator()() const { return "Meow"; }
};

struct vtable {
  // `operator()` is an invalid identifier so we must use a mangled name.
  std::string_view (*operator_call_fn)(const void* data);
};

class AnimalPtr {
  void* data_;
  const vtable* vtable_;

 public:
  template <typename T>
  AnimalPtr(T* t) : data_(t) {
    // Making the vtable `constexpr` avoids runtime cost (synchronization
    // between threads) when accessing static variables.
    constexpr static vtable vtable_for_type = {
        .operator_call_fn =
            +[](const void* data) {
              return static_cast<const T*>(data)->operator()();
            },
    };

    vtable_ = &vtable_for_type;
  }

  std::string_view operator()() const {  //
    return vtable_->operator_call_fn(data_);
  }
};

TEST(TutorialsVtables, VtableWithCallOperator) {
  Cat cat;

  AnimalPtr a(&cat);

  EXPECT_EQ(a(), "Meow");
}
}  // namespace xyz::tutorials::vtable_with_operators

namespace xyz::tutorials::name_mangling_for_vtable {

using std::meta::dealias;
using std::meta::is_const;
using std::meta::is_const_type;
using std::meta::is_function;
using std::meta::is_lvalue_reference_type;
using std::meta::is_operator_function;
using std::meta::is_pointer_type;
using std::meta::is_rvalue_reference_type;
using std::meta::is_special_member_function;
using std::meta::is_static_member;
using std::meta::parameters_of;
using std::meta::remove_const;
using std::meta::remove_pointer;
using std::meta::remove_reference;
using std::meta::type_of;
using std::views::filter;

struct Cat {
  std::string_view operator()() const { return "Meow"; }

  // Declared only, to exercise `mangle`'s handling of operator overloads
  // distinguished only by const-qualification and parameter type; never
  // called, so no definitions are needed.
  std::string_view operator()();
  std::string_view operator()(int) const;
  std::string_view operator()(int);

  std::string_view noise() const { return "Meow"; }

  std::string_view noise(int) const { return "Purr"; }

  // Declared only, to exercise `mangle_type`'s pointer/reference/const/alias
  // handling; never called, so no definitions are needed.
  std::string_view noise(int*) const;
  std::string_view noise(int&) const;
  std::string_view noise(const int&) const;
  std::string_view noise(std::size_t) const;

  // A class-type parameter, so `mangle_type` also exercises the
  // `mangle_qualified_name`/`sanitize` fallback for types `builtin_type_code`
  // doesn't cover; declared only, never called.
  std::string_view noise(std::string_view) const;
};

// `is_identifier_char` is evaluable at compile time.
constexpr bool is_identifier_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

// Unlike every other atom-building function in this file, `sanitize` is not
// collision-proof: it maps every character a C++ identifier can't contain
// (space, `::`, `<`, `>`, `,`, `*`, `&`, ...) onto the same `_`, so two
// different inputs can sanitize to identical text (e.g. `foo<int>` and a
// literal type named `foo_int_`). Because that collapsing happens inside a
// single length-prefixed atom, `mangle_atom`'s length prefix can't catch it.
// It exists only as the fallback for `mangle_qualified_name`, where fully
// decomposing template argument lists would need a lot more machinery for a
// tutorial than this file wants to carry.
consteval std::string sanitize(std::string_view text) {
  std::string out;
  for (char c : text) {
    out += is_identifier_char(c) ? c : '_';
  }
  return out;
}

consteval std::string decimal(std::size_t value) {
  std::array<char, std::numeric_limits<std::size_t>::digits10 + 1> buffer;
  auto result =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  return std::string(buffer.data(), result.ptr);
}

// Length-prefixed with no separator, matching the Itanium C++ ABI's own
// <source-name> production (`Foo` mangles as `3Foo`, not `3_Foo`). This
// stays unambiguous because every atom mangled here is either a genuine
// C++ identifier, which the grammar guarantees never starts with a digit,
// or one of this file's own fixed non-digit qualifier tags below, so a
// decoder can always tell where the greedily-read length ends and the
// payload begins.
consteval std::string mangle_atom(std::string_view text) {
  return decimal(text.size()) + std::string(text);
}

// Length-prefixes each `::`-separated component of a qualified name
// individually, so e.g. `A::B` can't collide with a hypothetical `A_B`.
consteval std::string mangle_qualified_name(std::string_view name) {
  std::string out;
  std::size_t pos = 0;
  while (true) {
    std::size_t next = name.find("::", pos);
    out += mangle_atom(sanitize(name.substr(pos, next - pos)));
    if (next == std::string_view::npos) break;
    pos = next + 2;  // Advance past "::".
  }
  return out;
}

// Itanium ABI <builtin-type> codes, so fundamental types mangle as a single
// letter (e.g. `std::size_t` -> `m`) instead of a sanitized spelling of
// their name. Dealiases its own argument, so e.g. `std::size_t` matches
// `unsigned long` regardless of whether the caller already dealiased it.
// Unmatched types fall through to `mangle_qualified_name`.
consteval std::string_view builtin_type_code(std::meta::info type) {
  type = dealias(type);
  if (type == ^^void) return "v";
  if (type == ^^bool) return "b";
  if (type == ^^char) return "c";
  if (type == ^^signed char) return "a";
  if (type == ^^unsigned char) return "h";
  if (type == ^^wchar_t) return "w";
  if (type == ^^char8_t) return "Du";
  if (type == ^^char16_t) return "Ds";
  if (type == ^^char32_t) return "Di";
  if (type == ^^short) return "s";
  if (type == ^^unsigned short) return "t";
  if (type == ^^int) return "i";
  if (type == ^^unsigned int) return "j";
  if (type == ^^long) return "l";
  if (type == ^^unsigned long) return "m";
  if (type == ^^long long) return "x";
  if (type == ^^unsigned long long) return "y";
  if (type == ^^float) return "f";
  if (type == ^^double) return "d";
  if (type == ^^long double) return "e";
  if (type == dealias(^^std::nullptr_t)) return "Dn";
  return "";
}

// Recurses through pointer/reference/const layers, tagging each with a
// single letter before descending to the pointee/referee, so `int*` and
// `int&` can no longer collapse onto the same name the way naive
// character-by-character sanitizing would. `dealias` resolves typedefs
// (e.g. `std::size_t`) to their underlying type first, so aliases of the
// same type always mangle identically. Tags match the Itanium ABI's own
// pointer/reference/cv-qualifier productions: P = pointer, R = lvalue
// reference, O = rvalue reference, K = const.
consteval std::string mangle_type(std::meta::info type) {
  type = dealias(type);
  if (is_const_type(type)) return "K" + mangle_type(remove_const(type));
  if (is_pointer_type(type)) return "P" + mangle_type(remove_pointer(type));
  if (is_lvalue_reference_type(type)) {
    return "R" + mangle_type(remove_reference(type));
  }
  if (is_rvalue_reference_type(type)) {
    return "O" + mangle_type(remove_reference(type));
  }
  std::string_view builtin = builtin_type_code(type);
  if (!builtin.empty()) return std::string(builtin);
  return mangle_qualified_name(std::meta::display_string_of(type));
}

// Real Itanium ABI <operator-name> mnemonics where this file has one;
// anything else falls back to its sanitized symbol.
// The real Itanium ABI <operator-name> mnemonics for every operator this
// file's reflection library can name. Falling back to `sanitize` here (as
// an earlier version of this file did) is not safe: sanitizing a bare
// symbol collapses distinct operators onto the same text (`+` and `-` both
// become `_`), silently colliding two different overloads' mangled names.
consteval std::string operator_code(std::meta::operators op) {
  using enum std::meta::operators;
  switch (op) {
    case op_new:
      return "nw";
    case op_delete:
      return "dl";
    case op_array_new:
      return "na";
    case op_array_delete:
      return "da";
    case op_co_await:
      return "aw";
    case op_parentheses:
      return "cl";
    case op_square_brackets:
      return "ix";
    case op_arrow:
      return "pt";
    case op_arrow_star:
      return "pm";
    case op_tilde:
      return "co";
    case op_exclamation:
      return "nt";
    case op_plus:
      return "pl";
    case op_minus:
      return "mi";
    case op_star:
      return "ml";
    case op_slash:
      return "dv";
    case op_percent:
      return "rm";
    case op_caret:
      return "eo";
    case op_ampersand:
      return "an";
    case op_equals:
      return "aS";
    case op_pipe:
      return "or";
    case op_plus_equals:
      return "pL";
    case op_minus_equals:
      return "mI";
    case op_star_equals:
      return "mL";
    case op_slash_equals:
      return "dV";
    case op_percent_equals:
      return "rM";
    case op_caret_equals:
      return "eO";
    case op_ampersand_equals:
      return "aN";
    case op_pipe_equals:
      return "oR";
    case op_equals_equals:
      return "eq";
    case op_exclamation_equals:
      return "ne";
    case op_less:
      return "lt";
    case op_greater:
      return "gt";
    case op_less_equals:
      return "le";
    case op_greater_equals:
      return "ge";
    case op_spaceship:
      return "ss";
    case op_ampersand_ampersand:
      return "aa";
    case op_pipe_pipe:
      return "oo";
    case op_less_less:
      return "ls";
    case op_greater_greater:
      return "rs";
    case op_less_less_equals:
      return "lS";
    case op_greater_greater_equals:
      return "rS";
    case op_plus_plus:
      return "pp";
    case op_minus_minus:
      return "mm";
    case op_comma:
      return "cm";
  }
}

consteval std::string base_name_of(std::meta::info fn) {
  // `operator()` has no identifier, so `identifier_of` would throw; its
  // name is built from the operator's Itanium mnemonic instead.
  if (is_operator_function(fn)) {
    return operator_code(std::meta::operator_of(fn));
  }
  return std::string(identifier_of(fn));
}

// Distinguishes overloads by folding each parameter's type and the
// function's const-qualification into the name, so `noise()` and
// `noise(int)` no longer collide on the same mangled name. `mangle_atom`'s
// length prefixes mean the result can start with a digit, which is not a
// valid way to start a C++ identifier, so the whole name gets a fixed
// `function` prefix, playing the same role as Itanium's own `_Z`.
consteval std::string_view mangle(std::meta::info fn) {
  std::string name = "function" + mangle_atom(base_name_of(fn));
  auto params = parameters_of(fn);
  for (auto param : params) {
    name += mangle_type(type_of(param));
  }
  if (is_const(fn)) name += "K";
  return std::define_static_string(name);
}

TEST(TutorialsVtables, NameMangling) {
  // P2996 has no syntax for disambiguating an overload set by writing a
  // parameter list after `^^`; reflecting an id-expression that names an
  // overload set is ill-formed. Picking one overload out requires walking
  // `members_of` instead. The implicit copy/move-assignment operators are
  // themselves operator functions, so they're filtered out alongside the
  // other special member functions.
  constexpr auto member_functions = std::define_static_array(
      members_of(^^Cat, std::meta::access_context::unprivileged()) |
      filter(is_function) | filter(std::not_fn(is_static_member)) |
      filter(std::not_fn(is_special_member_function)));

  // Indices below rely on `members_of` enumerating `Cat` in declaration
  // order; each is labelled with the overload it's expected to be.
  XYZ_CONSTEVAL_CHECK(mangle(member_functions[0]) ==
                      "function2clK");  // operator()() const
  XYZ_CONSTEVAL_CHECK(mangle(member_functions[1]) ==
                      "function2cl");  // operator()()
  XYZ_CONSTEVAL_CHECK(mangle(member_functions[2]) ==
                      "function2cliK");  // operator()(int) const
  XYZ_CONSTEVAL_CHECK(mangle(member_functions[3]) ==
                      "function2cli");  // operator()(int)
  XYZ_CONSTEVAL_CHECK(mangle(member_functions[4]) ==
                      "function5noiseK");  // noise() const
  XYZ_CONSTEVAL_CHECK(mangle(member_functions[5]) ==
                      "function5noiseiK");  // noise(int) const
  XYZ_CONSTEVAL_CHECK(mangle(member_functions[6]) ==
                      "function5noisePiK");  // noise(int*) const
  XYZ_CONSTEVAL_CHECK(mangle(member_functions[7]) ==
                      "function5noiseRiK");  // noise(int&) const
  XYZ_CONSTEVAL_CHECK(mangle(member_functions[8]) ==
                      "function5noiseRKiK");  // noise(const int&) const
  XYZ_CONSTEVAL_CHECK(mangle(member_functions[9]) ==
                      "function5noisemK");  // noise(std::size_t) const
  XYZ_CONSTEVAL_CHECK(
      mangle(member_functions[10]) ==
      "function5noise3std23basic_string_view_char_K");  // noise(std::string_view)
                                                        // const
}

}  // namespace xyz::tutorials::name_mangling_for_vtable
