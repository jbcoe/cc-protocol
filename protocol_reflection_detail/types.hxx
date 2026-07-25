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
// Synthesizing member and vtable-entry function types from a reflected
// member function, via std::meta::substitute over a variadic alias
// template.
#ifndef XYZ_PROTOCOL_REFLECTION_DETAIL_TYPES_HXX_
#define XYZ_PROTOCOL_REFLECTION_DETAIL_TYPES_HXX_

#include <meta>
#include <vector>

namespace xyz::reflection_detail {

// `member`'s parameter types, dealiased. std::meta::parameters_of never
// yields the implicit object parameter, so none of these are it.
consteval std::vector<std::meta::info> parameter_types_of(
    std::meta::info member) {
  std::vector<std::meta::info> types;
  for (std::meta::info parameter : std::meta::parameters_of(member)) {
    types.push_back(std::meta::dealias(std::meta::type_of(parameter)));
  }
  return types;
}

template <typename R, typename... Ps>
using function_type_t = R(Ps...);

// `member`'s own call signature as an ordinary function type, R(Ps...).
consteval std::meta::info member_function_type(std::meta::info member) {
  std::vector<std::meta::info> args{
      std::meta::dealias(std::meta::return_type_of(member))};
  for (std::meta::info parameter_type : parameter_types_of(member)) {
    args.push_back(parameter_type);
  }
  return std::meta::substitute(^^function_type_t, args);
}

template <typename R, typename... Ps>
using fn_ptr_t = R (*)(Ps...);

// The vtable entry's pointer-to-function type for `member`:
// R (*)(ErasedPointer, Ps...), with the member's implicit object parameter
// replaced by `erased_pointer_type` (typically ^^void* for a non-const
// member, ^^const void* for a const one).
consteval std::meta::info vtable_entry_pointer_type(
    std::meta::info member, std::meta::info erased_pointer_type) {
  std::vector<std::meta::info> args{
      std::meta::dealias(std::meta::return_type_of(member)),
      erased_pointer_type};
  for (std::meta::info parameter_type : parameter_types_of(member)) {
    args.push_back(parameter_type);
  }
  return std::meta::substitute(^^fn_ptr_t, args);
}

}  // namespace xyz::reflection_detail

#endif  // XYZ_PROTOCOL_REFLECTION_DETAIL_TYPES_HXX_
