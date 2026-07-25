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
// The static function stored in a vtable entry: casts the erased pointer
// back to Implementation and forwards the call through MergedCandidates, a
// default-constructible callable invoked as
// MergedCandidates{}(implementation_reference, args...). This thunk only
// requires MergedCandidates to be callable that way, so it can be tested
// here against a hand-rolled fake candidate.
#ifndef XYZ_PROTOCOL_REFLECTION_DETAIL_THUNK_HXX_
#define XYZ_PROTOCOL_REFLECTION_DETAIL_THUNK_HXX_

#include <type_traits>
#include <utility>

namespace xyz::reflection_detail {

template <typename Implementation, typename MergedCandidates,
          typename Signature, bool ConstErased, bool IsNoexcept>
struct erased_call_thunk;

template <typename Implementation, typename MergedCandidates, typename R,
          typename... Args, bool ConstErased, bool IsNoexcept>
struct erased_call_thunk<Implementation, MergedCandidates, R(Args...),
                         ConstErased, IsNoexcept> {
  using erased_pointer_type =
      std::conditional_t<ConstErased, const void*, void*>;
  using implementation_pointer_type =
      std::conditional_t<ConstErased, const Implementation*, Implementation*>;

  static R call(erased_pointer_type erased, Args... args) noexcept(IsNoexcept) {
    auto* implementation = static_cast<implementation_pointer_type>(erased);
    return MergedCandidates{}(*implementation, std::forward<Args>(args)...);
  }
};

}  // namespace xyz::reflection_detail

#endif  // XYZ_PROTOCOL_REFLECTION_DETAIL_THUNK_HXX_
