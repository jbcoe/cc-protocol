/* Copyright (c) 2026 The XYZ Protocol Authors. All Rights Reserved.

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
// Erased-call thunk for the C++26 reflection backend: the static function
// shape a vtable entry points at. Split out of protocol_reflection.h
// because this piece is self-contained: it only needs an already-built
// Signature and a reflected MergedCandidates type, constructible from a
// pointer and callable with the given parameters. It never calls into
// member enumeration, naming, type synthesis, or candidate resolution
// itself; those are only used by callers deciding what to plug in. That
// makes it independently testable with a hand-rolled fake candidate type:
// thunk_test.cc includes this header directly rather than the whole
// backend.
#ifndef XYZ_PROTOCOL_REFLECTION_THUNK_H_
#define XYZ_PROTOCOL_REFLECTION_THUNK_H_

#ifndef __cpp_impl_reflection
#error \
    "This header requires a compiler with C++26 reflection support (GCC 16+, -std=c++26 -freflection)."
#endif

#include <meta>
#include <type_traits>
#include <utility>

namespace xyz {
namespace reflection_detail {

using std::meta::info;

// ---------------------------------------------------------------------------
// Erased-call thunks: the static functions vtable entries point at.
// Signature exactly mirrors the interface member (exact parameter types,
// matching noexcept), with a leading erased pointer. ConstErased selects the
// const_view flavour (const void* + const implementation access).
// MergedCandidates is the make_candidate_overload_set type built
// for this interface member: the thunk constructs an instance bound to
// self and calls through it, so the compiler's real overload resolution,
// not this thunk, picks which candidate runs.
// ---------------------------------------------------------------------------

template <typename Implementation, info MergedCandidates, typename Signature,
          bool ConstErased, bool IsNoexcept>
struct erased_call_thunk;

template <typename Implementation, info MergedCandidates, typename ReturnType,
          typename... ParameterTypes, bool ConstErased, bool IsNoexcept>
struct erased_call_thunk<Implementation, MergedCandidates,
                         ReturnType(ParameterTypes...), ConstErased,
                         IsNoexcept> {
  using ErasedPointer = std::conditional_t<ConstErased, const void*, void*>;
  using SelfPointer =
      std::conditional_t<ConstErased, const Implementation*, Implementation*>;

  static ReturnType call(ErasedPointer erased,
                         ParameterTypes... parameters) noexcept(IsNoexcept) {
    auto* self = static_cast<SelfPointer>(erased);
    using Candidates = [:MergedCandidates:];
    Candidates candidates(self);
    if constexpr (std::is_void_v<ReturnType>) {
      candidates(std::forward<ParameterTypes>(parameters)...);
    } else {
      return candidates(std::forward<ParameterTypes>(parameters)...);
    }
  }
};

}  // namespace reflection_detail
}  // namespace xyz

#endif  // XYZ_PROTOCOL_REFLECTION_THUNK_H_
