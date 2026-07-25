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
#include "protocol_reflection_detail/conformance.hxx"

#include <gtest/gtest.h>

#include "protocol_reflection_detail/members.hxx"

namespace {

using xyz::reflection_detail::candidate_overload_set_type;
using xyz::reflection_detail::interface_member_functions;
using xyz::reflection_detail::parameter_types_of;
using xyz::reflection_detail::reflection_protocol_concept;
using xyz::reflection_detail::reflection_protocol_const_concept;
using xyz::reflection_detail::resolve_implementation_candidates;

struct Interface {
  int compute(int) const;
  double compute(double) const;
  void set_value(int);
};

struct ExactMatch {
  int compute(int x) const { return x * 2; }

  double compute(double x) const { return x * 3.0; }

  void set_value(int v) { value = v; }

  int value = 0;
};

struct MissingMethod {
  int compute(int x) const { return x * 2; }

  double compute(double x) const { return x * 3.0; }

  // No set_value: does not model Interface at all.
};

struct ConstOnly {
  int compute(int x) const { return x * 2; }

  double compute(double x) const { return x * 3.0; }

  // No set_value: models Interface's const subset only.
};

struct DiscardingSetValue {
  int compute(int x) const { return x * 2; }

  double compute(double x) const { return x * 3.0; }

  // Interface's set_value returns void; this candidate returns int instead.
  // DRAFT.md's "Relaxed structural subtyping" design alternative requires
  // exactly matching signatures, so this does not conform even though the
  // return value could just be discarded.
  int set_value(int v) {
    value = v;
    return v;
  }

  int value = 0;
};

TEST(ReflectionProtocolConcept, NonVoidReturnDoesNotModelAVoidInterfaceMember) {
  static_assert(!reflection_protocol_concept<DiscardingSetValue, Interface>);
}

TEST(ReflectionProtocolConcept, ExactMatchModelsTheFullInterface) {
  static_assert(reflection_protocol_concept<ExactMatch, Interface>);
}

TEST(ReflectionProtocolConcept, MissingMethodDoesNotModelTheInterface) {
  static_assert(!reflection_protocol_concept<MissingMethod, Interface>);
}

TEST(ReflectionProtocolConstConcept, ConstOnlyModelsJustTheConstSubset) {
  static_assert(reflection_protocol_const_concept<ConstOnly, Interface>);
  static_assert(!reflection_protocol_concept<ConstOnly, Interface>);
}

// A single-method (non-overloaded) interface, so there's no other candidate
// an implicit conversion could route the call through: isolates the
// wrong-constness rejection case from overload-resolution noise (Interface
// above is overloaded, so a non-const candidate being rejected can still
// leave a convertible-argument overload standing).
struct SingleMethodInterface {
  int compute(int) const;
};

struct NonConstCompute {
  int compute(int x) { return x * 2; }  // non-const; interface wants const
};

TEST(ReflectionProtocolConcept, WrongConstnessDoesNotModelTheInterface) {
  static_assert(
      !reflection_protocol_concept<NonConstCompute, SingleMethodInterface>);
}

consteval std::meta::info compute_int_overload() {
  for (std::meta::info member : interface_member_functions(^^Interface)) {
    if (std::meta::identifier_of(member) == "compute" &&
        parameter_types_of(member)[0] == ^^int) {
      return member;
    }
  }
  throw std::meta::exception("compute(int) not found", ^^void);
}

consteval std::meta::info compute_merged_type() {
  return candidate_overload_set_type(
      resolve_implementation_candidates(compute_int_overload(), ^^ExactMatch),
      ^^ExactMatch&);
}

using ComputeMerged = typename[:compute_merged_type():];

TEST(CandidateOverloadSet, RealOverloadResolutionPicksTheMatchingCandidate) {
  ExactMatch impl;
  ComputeMerged merged;
  EXPECT_EQ(merged(impl, 5), 10);
  EXPECT_EQ(merged(impl, 2.5), 7.5);
}

}  // namespace
