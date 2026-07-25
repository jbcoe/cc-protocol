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
// Enumerating an interface type's dispatchable member functions.
#ifndef XYZ_PROTOCOL_REFLECTION_DETAIL_MEMBERS_HXX_
#define XYZ_PROTOCOL_REFLECTION_DETAIL_MEMBERS_HXX_

#include <meta>
#include <vector>

namespace xyz::reflection_detail {

consteval bool is_interface_member_function(std::meta::info member) {
  return std::meta::is_function(member) && std::meta::is_public(member) &&
         !std::meta::is_static_member(member) &&
         !std::meta::is_special_member_function(member) &&
         !std::meta::is_function_template(member);
}

// Every member of `type` for which is_interface_member_function holds, in
// declaration order.
consteval std::vector<std::meta::info> interface_member_functions(
    std::meta::info type) {
  std::vector<std::meta::info> result;
  for (std::meta::info member :
       std::meta::members_of(type, std::meta::access_context::current())) {
    if (is_interface_member_function(member)) {
      result.push_back(member);
    }
  }
  return result;
}

}  // namespace xyz::reflection_detail

#endif  // XYZ_PROTOCOL_REFLECTION_DETAIL_MEMBERS_HXX_
