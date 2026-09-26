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
#ifndef XYZ_PROTOCOL_OVERLOAD_SPEC_HH_
#define XYZ_PROTOCOL_OVERLOAD_SPEC_HH_

#include <initializer_list>
#include <meta>
#include <utility>
#include <vector>

namespace xyz::detail {

template <bool IsNoexcept, typename R, typename... Args>
using fn_ptr_t = R (*)(Args...) noexcept(IsNoexcept);

// The function-pointer type R(*)(Leading..., Args...) noexcept(...) built from
// `member`'s return type, parameter types and noexcept-ness. A thunk passes no
// `leading` types; a vtable entry passes the erased object pointer, so the two
// differ in that parameter alone and a thunk's partial specialisation always
// matches its vtable entry.
consteval std::meta::info function_pointer_type_of(
    std::meta::info member,
    std::initializer_list<std::meta::info> leading = {}) {
  std::vector<std::meta::info> fn_args{
      std::meta::reflect_constant(is_noexcept(member)),
      dealias(return_type_of(member))};
  fn_args.append_range(leading);
  std::vector<std::meta::info> member_parameters = parameters_of(member);
  for (std::meta::info parameter : member_parameters) {
    fn_args.push_back(type_of(parameter));
  }
  return substitute(^^fn_ptr_t, fn_args);
}

enum class member_options {
  none = 0,
  is_const = 1 << 0,
  is_lvalue = 1 << 1,
  is_rvalue = 1 << 2
};

constexpr member_options operator|(member_options lhs, member_options rhs) {
  return static_cast<member_options>(std::to_underlying(lhs) |
                                     std::to_underlying(rhs));
}

constexpr member_options& operator|=(member_options& lhs, member_options rhs) {
  return lhs = (lhs | rhs);
}

constexpr bool operator&(member_options lhs, member_options rhs) {
  return static_cast<bool>(std::to_underlying(lhs) & std::to_underlying(rhs));
}

// One overload of a synthesised member function or operator: the interface
// member (which names its vtable entry) and the member options (const and ref
// qualifiers) of the generated wrapper.
template <std::meta::info Member, member_options MemberOptions>
struct overload_spec {};

}  // namespace xyz::detail

#endif  // XYZ_PROTOCOL_OVERLOAD_SPEC_HH_
