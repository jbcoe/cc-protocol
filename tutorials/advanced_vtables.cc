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
#include <functional>
#include <limits>
#include <meta>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "consteval_check.h"

// A study of using vtables for overloaded member functions and `operator()`.
// This tutorial follows on from the work in `tutorials/polymorphism.cc` and
// `tutorials/reflection.cc`

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

// An illustration of a name mangling regime that can be used to disambiguate
// names for function overloads and represent operators whose names are not
// valid identifiers.

namespace xyz::tutorials::name_mangling_for_vtable {

using std::meta::dealias;
using std::meta::is_const;
using std::meta::is_const_type;
using std::meta::is_function;
using std::meta::is_fundamental_type;
using std::meta::is_lvalue_reference_qualified;
using std::meta::is_lvalue_reference_type;
using std::meta::is_operator_function;
using std::meta::is_pointer_type;
using std::meta::is_rvalue_reference_qualified;
using std::meta::is_rvalue_reference_type;
using std::meta::is_special_member_function;
using std::meta::is_static_member;
using std::meta::is_volatile;
using std::meta::parameters_of;
using std::meta::remove_const;
using std::meta::remove_pointer;
using std::meta::remove_reference;
using std::meta::type_of;
using std::views::filter;

struct Cat {
  std::string_view operator()() const;
  std::string_view operator()();
  std::string_view operator()(int) const;
  std::string_view operator()(int);

  std::string_view operator[](int) const;

  std::string_view operator->() const;

  std::string_view noise() const;
  std::string_view noise(int) const;
  std::string_view noise(int*) const;
  std::string_view noise(int&) const;
  std::string_view noise(const int&) const;
  std::string_view noise(std::size_t) const;
  std::string_view noise(std::string_view) const;

  std::string_view growl() &;
  std::string_view growl() &&;
  std::string_view growl() const;
  std::string_view growl() const volatile;
};

consteval std::string decimal(std::size_t value) {
  std::array<char, std::numeric_limits<std::size_t>::digits10 + 1> buffer;
  auto result =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  return std::string(buffer.data(), result.ptr);
}

// Length-prefixed with no separator, modelled on the Itanium ABI.
consteval std::string mangle_atom(std::string_view text) {
  return decimal(text.size()) + std::string(text);
}

// Forward-declared: mutually recursive with `mangle_scope_component` via a
// template argument that is itself a class-template specialization.
consteval std::string mangle_type(std::meta::info type);

// Mangles one named scope level (a namespace or a class), folding in
// template arguments when `scope` is a class-template specialization.
consteval std::string mangle_scope_component(std::meta::info scope) {
  std::meta::info named =
      has_template_arguments(scope) ? template_of(scope) : scope;
  std::string out = mangle_atom(std::string(identifier_of(named)));
  if (has_template_arguments(scope)) {
    out += "I";
    auto args = template_arguments_of(scope);
    for (std::meta::info arg : args) {
      if (!is_type(arg)) {
        // Limited template-argument handling for brevity.
        throw std::runtime_error(
            "mangle_scope_component: only type template arguments are "
            "supported");
      }
      out += mangle_type(arg);
    }
    out += "E";
  }
  return out;
}

// Walks `parent_of` up to (not including) the global namespace, then wraps
// 2+ levels in Itanium's N...E delimiters so a qualified name can't be
// confused with a sequence of unqualified ones.
consteval std::string mangle_qualified_name(std::meta::info type) {
  std::vector<std::string> atoms;
  std::meta::info scope = dealias(type);
  while (!(is_namespace(scope) && !has_identifier(scope))) {
    atoms.push_back(mangle_scope_component(scope));
    scope = dealias(parent_of(scope));
  }
  std::string joined;
  for (auto it = atoms.rbegin(); it != atoms.rend(); ++it) joined += *it;
  return atoms.size() > 1 ? "N" + joined + "E" : joined;
}

// Itanium ABI <builtin-type> codes for fundamental types.
consteval std::optional<std::string_view> builtin_type_code(
    std::meta::info type) {
  type = dealias(type);
  if (type == ^^int) return "i";
  if (type == ^^char) return "c";
  if (type == dealias(^^std::size_t)) return "m";
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
  return mangle_qualified_name(type);
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

// Distinguishes overloads by folding the function's cv- and
// ref-qualification, then each parameter's type, into the name. Matches the
// Itanium ABI's own placement: cv-/ref-qualifiers sit inside `N...E`,
// wrapping the function name, ahead of the parameter list.
consteval std::string_view mangle(std::meta::info fn) {
  std::string quals;
  if (is_volatile(fn)) quals += "V";
  if (is_const(fn)) quals += "K";
  if (is_lvalue_reference_qualified(fn)) quals += "R";
  if (is_rvalue_reference_qualified(fn)) quals += "O";
  std::string atom = mangle_atom(base_name_of(fn));
  std::string name = "fn_" + (quals.empty() ? atom : "N" + quals + atom + "E");
  auto params = parameters_of(fn);
  for (auto param : params) {
    name += mangle_type(type_of(param));
  }
  return std::define_static_string(name);
}

// `Type`'s public member functions with a name to mangle, in declaration
// order: named functions plus operators, excluding constructors and other
// unnamed special members that `identifier_of` can't handle.
template <std::meta::info Type>
constexpr auto member_functions_of = std::define_static_array(
    members_of(Type, std::meta::access_context::unprivileged()) |
    filter(is_function) | filter(std::not_fn(is_static_member)) |
    filter([](std::meta::info member) consteval {
      return (has_identifier(member) || is_operator_function(member)) &&
             !is_special_member_function(member);
    }));

TEST(TutorialsVtables, NameMangling) {
  consteval {
    auto m_fns = member_functions_of<^^Cat>;

    // operator()() const
    XYZ_CONSTEVAL_CHECK(mangle(m_fns[0]) == "fn_NK2clE");
    // operator()()
    XYZ_CONSTEVAL_CHECK(mangle(m_fns[1]) == "fn_2cl");
    // operator()(int) const
    XYZ_CONSTEVAL_CHECK(mangle(m_fns[2]) == "fn_NK2clEi");
    // operator()(int)
    XYZ_CONSTEVAL_CHECK(mangle(m_fns[3]) == "fn_2cli");
    // operator[](int) const
    XYZ_CONSTEVAL_CHECK(mangle(m_fns[4]) == "fn_NK2ixEi");
    // operator->() const
    XYZ_CONSTEVAL_CHECK(mangle(m_fns[5]) == "fn_NK2ptE");
    // noise() const
    XYZ_CONSTEVAL_CHECK(mangle(m_fns[6]) == "fn_NK5noiseE");
    // noise(int) const
    XYZ_CONSTEVAL_CHECK(mangle(m_fns[7]) == "fn_NK5noiseEi");
    // noise(int*) const
    XYZ_CONSTEVAL_CHECK(mangle(m_fns[8]) == "fn_NK5noiseEPi");
    // noise(int&) const
    XYZ_CONSTEVAL_CHECK(mangle(m_fns[9]) == "fn_NK5noiseERi");
    // noise(const int&) const
    XYZ_CONSTEVAL_CHECK(mangle(m_fns[10]) == "fn_NK5noiseERKi");
    // noise(std::size_t) const
    XYZ_CONSTEVAL_CHECK(mangle(m_fns[11]) == "fn_NK5noiseEm");
    // noise(std::string_view) const
    XYZ_CONSTEVAL_CHECK(
        mangle(m_fns[12]) ==
        "fn_NK5noiseEN3std17basic_string_viewIcN3std11char_traitsIcEEEE");
    // growl() &
    XYZ_CONSTEVAL_CHECK(mangle(m_fns[13]) == "fn_NR5growlE");
    // growl() &&
    XYZ_CONSTEVAL_CHECK(mangle(m_fns[14]) == "fn_NO5growlE");
    // growl() const
    XYZ_CONSTEVAL_CHECK(mangle(m_fns[15]) == "fn_NK5growlE");
    // growl() const volatile
    XYZ_CONSTEVAL_CHECK(mangle(m_fns[16]) == "fn_NVK5growlE");
  }
}

}  // namespace xyz::tutorials::name_mangling_for_vtable

// We can use the mangled names when writing the vtable to allow functions to be
// disambiguated.

namespace xyz::tutorials::name_mangled_vtable {

using std::meta::dealias;
using std::meta::parameters_of;
using std::meta::type_of;
using xyz::tutorials::name_mangling_for_vtable::mangle;
using xyz::tutorials::name_mangling_for_vtable::member_functions_of;

struct Cat {
  // For simplicity, we consider only const member functions.
  std::string_view noise() const { return "Meow"; }

  std::string_view noise(int) const { return "Purr"; }

  std::string_view operator()() const { return "Meow"; }

  std::string_view operator()(int) const { return "Purr"; }
};

template <typename R, typename... Args>
using const_fn_ptr_t = R (*)(const void*, Args...);

template <std::meta::info Interface>
consteval std::vector<std::meta::info> mangled_vtable_specs() {
  std::vector<std::meta::info> specs;
  template for (constexpr std::meta::info fn : member_functions_of<Interface>) {
    std::vector<std::meta::info> fn_args{dealias(return_type_of(fn))};
    for (std::meta::info param : parameters_of(fn)) {
      fn_args.push_back(dealias(type_of(param)));
    }
    std::meta::info fn_ptr_type = substitute(^^const_fn_ptr_t, fn_args);
    specs.push_back(data_member_spec(fn_ptr_type, {.name = mangle(fn)}));
  }
  return specs;
}

template <typename T>
struct vtable_generator {
  struct vtable;
  consteval { define_aggregate(^^vtable, mangled_vtable_specs<^^T>()); }
};

class AnimalPtr {
  using vtable_t = vtable_generator<Cat>::vtable;
  const void* data_;
  const vtable_t* vtable_;

 public:
  template <typename T>
  explicit AnimalPtr(const T* animal) : data_(animal) {
    constexpr static vtable_t vtable_for_type{
        .fn_NK5noiseE =
            +[](const void* data) {
              return static_cast<const T*>(data)->noise();
            },
        .fn_NK5noiseEi =
            +[](const void* data, int x) {
              return static_cast<const T*>(data)->noise(x);
            },
        .fn_NK2clE =
            +[](const void* data) {  //
              return (*static_cast<const T*>(data))();
            },
        .fn_NK2clEi = +[](const void* data,
                          int x) {  //
          return (*static_cast<const T*>(data))(x);
        }};
    vtable_ = &vtable_for_type;
  }

  std::string_view noise() const {  //
    return vtable_->fn_NK5noiseE(data_);
  }

  std::string_view noise(int x) const {
    return vtable_->fn_NK5noiseEi(data_, x);
  }

  std::string_view operator()() const {  //
    return vtable_->fn_NK2clE(data_);
  }

  std::string_view operator()(int x) const {
    return vtable_->fn_NK2clEi(data_, x);
  }
};

TEST(TutorialsVtables, NameMangledVtable) {
  Cat cat;
  AnimalPtr p(&cat);

  EXPECT_EQ(p.noise(), "Meow");
  EXPECT_EQ(p.noise(3), "Purr");
  EXPECT_EQ(p(), "Meow");
  EXPECT_EQ(p(3), "Purr");
}

}  // namespace xyz::tutorials::name_mangled_vtable
