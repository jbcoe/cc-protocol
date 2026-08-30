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
#include <optional>
#include <ranges>
#include <stdexcept>
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
using std::meta::is_fundamental_type;
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

  // Everything below is declared only, never called: it exists purely for
  // reflection to walk, distinguished by const-qualification and parameter
  // type.
  std::string_view operator()();
  std::string_view operator()(int) const;
  std::string_view operator()(int);

  // Exercises `operator_code` for more than one operator.
  std::string_view operator[](int) const;
  std::string_view operator->() const;

  std::string_view noise() const { return "Meow"; }

  std::string_view noise(int) const { return "Purr"; }

  // Exercises `mangle_type`'s pointer/reference/const/alias handling.
  std::string_view noise(int*) const;
  std::string_view noise(int&) const;
  std::string_view noise(const int&) const;
  std::string_view noise(std::size_t) const;

  // A class type, exercising `mangle_qualified_name`/`sanitize`, the
  // fallback for types `builtin_type_code` doesn't cover.
  std::string_view noise(std::string_view) const;
};

constexpr bool is_identifier_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

// Unlike everything else here, not collision-proof: mapping every
// non-identifier character onto `_` means e.g. `foo<int>` and a type
// literally named `foo_int_` sanitize identically, inside a single
// `mangle_atom` call the length prefix can't see into. Used only as
// `mangle_qualified_name`'s fallback for template argument lists, which
// this tutorial doesn't decompose further.
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

// Length-prefixed with no separator, matching the Itanium ABI's own
// <source-name> production (`Foo` mangles as `3Foo`, not `3_Foo`). Stays
// unambiguous because every atom is either a genuine C++ identifier, which
// never starts with a digit, or one of this file's own non-digit tags, so
// a decoder always knows where the length ends and the payload begins.
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

// Itanium ABI <builtin-type> codes for fundamental types.
consteval std::optional<std::string_view> builtin_type_code(
    std::meta::info type) {
  type = dealias(type);
  if (type == ^^int) return "i";
  if (type == ^^unsigned long) return "m";
  if (is_fundamental_type(type)) {
    // Limited type handling for brevity.
    throw std::runtime_error(
        "builtin_type_code: no mnemonic for this fundamental type");
  }
  return std::nullopt;
}

// Recurses through pointer/reference/const layers, tagging each with a
// single (Itanium ABI) letter, then descends to the pointee/referee.
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
  if (auto builtin = builtin_type_code(type)) return std::string(*builtin);
  return mangle_qualified_name(std::meta::display_string_of(type));
}

// Itanium ABI <operator-name> mnemonics for operators.
consteval std::string operator_code(std::meta::operators op) {
  using enum std::meta::operators;
  switch (op) {
    case op_parentheses:
      return "cl";
    case op_square_brackets:
      return "ix";
    case op_arrow:
      return "pt";
    default:
      // Limited operator handling for brevity.
      throw std::runtime_error("operator_code: no mnemonic for this operator");
  }
}

// `identifier_of` throws for `operator()`, which has no identifier; use
// its Itanium mnemonic instead.
consteval std::string base_name_of(std::meta::info fn) {
  if (is_operator_function(fn)) {
    return operator_code(std::meta::operator_of(fn));
  }
  return std::string(identifier_of(fn));
}

// Distinguishes overloads by folding each parameter's type and the
// function's const-qualification into the name.
consteval std::string_view mangle(std::meta::info fn) {
  std::string name = "fn_" + mangle_atom(base_name_of(fn));
  auto params = parameters_of(fn);
  for (auto param : params) {
    name += mangle_type(type_of(param));
  }
  if (is_const(fn)) name += "K";
  return std::define_static_string(name);
}

TEST(TutorialsVtables, NameMangling) {
  constexpr auto m_fns = std::define_static_array(
      members_of(^^Cat, std::meta::access_context::unprivileged()) |
      filter(is_function) | filter(std::not_fn(is_static_member)) |
      filter(std::not_fn(is_special_member_function)));

  // operator()() const
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[0]) == "fn_2clK");
  // operator()()
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[1]) == "fn_2cl");
  // operator()(int) const
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[2]) == "fn_2cliK");
  // operator()(int)
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[3]) == "fn_2cli");
  // operator[](int) const
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[4]) == "fn_2ixiK");
  // operator->() const
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[5]) == "fn_2ptK");
  // noise() const
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[6]) == "fn_5noiseK");
  // noise(int) const
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[7]) == "fn_5noiseiK");
  // noise(int*) const
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[8]) == "fn_5noisePiK");
  // noise(int&) const
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[9]) == "fn_5noiseRiK");
  // noise(const int&) const
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[10]) == "fn_5noiseRKiK");
  // noise(std::size_t) const
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[11]) == "fn_5noisemK");
  // noise(std::string_view) const
  XYZ_CONSTEVAL_CHECK(mangle(m_fns[12]) ==
                      "fn_5noise3std23basic_string_view_char_K");
}

}  // namespace xyz::tutorials::name_mangling_for_vtable
