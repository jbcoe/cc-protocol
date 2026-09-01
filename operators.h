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
#ifndef XYZ_REFLECTION_OPERATORS_H_
#define XYZ_REFLECTION_OPERATORS_H_

// The overloadable operators a protocol interface may declare as member
// functions and have dispatched through the vtable, as a single X-macro table
// consumed by both the protocol machinery and the name mangler. Each entry is
// X(name, token, mangling_code): `name` is the operator's identifier here and
// the suffix of the std::meta::operators::op_<name> enumerator that reflection
// reports for it; `token` spells it in source; `mangling_code` is its Itanium
// ABI <operator-name>, used to name the operator's vtable entry.
//
// The table is split by call shape. A general operator's thunk mirrors the
// interface member's own parameter list, so one variadic thunk covers its unary
// and binary forms and, for `++`/`--`, its prefix and postfix forms. A nullary
// operator (`->`, `~`, `!`) must take no parameters, which the compiler checks
// against the thunk's declared parameter list, so its thunk is generated
// without a parameter pack.
//
// operator=, operator new/delete (and array forms), operator co_await and the
// conversion operators are absent. A protocol's own assignment operators hide
// any inherited operator=; new/delete are static allocation functions rather
// than value operations; co_await and conversions are out of scope.

// Passing a comma as a macro argument needs an indirection: spelled directly it
// would separate arguments.
#define XYZ_REFLECTION_COMMA ,

#define XYZ_REFLECTION_FOR_EACH_GENERAL_OPERATOR(X) \
  X(parentheses, (), "cl")                          \
  X(square_brackets, [], "ix")                      \
  X(arrow_star, ->*, "pm")                          \
  X(plus, +, "pl")                                  \
  X(minus, -, "mi")                                 \
  X(star, *, "ml")                                  \
  X(slash, /, "dv")                                 \
  X(percent, %, "rm")                               \
  X(caret, ^, "eo")                                 \
  X(ampersand, &, "an")                             \
  X(pipe, |, "or")                                  \
  X(plus_equals, +=, "pL")                          \
  X(minus_equals, -=, "mI")                         \
  X(star_equals, *=, "mL")                          \
  X(slash_equals, /=, "dV")                         \
  X(percent_equals, %=, "rM")                       \
  X(caret_equals, ^=, "eO")                         \
  X(ampersand_equals, &=, "aN")                     \
  X(pipe_equals, |=, "oR")                          \
  X(equals_equals, ==, "eq")                        \
  X(exclamation_equals, !=, "ne")                   \
  X(less, <, "lt")                                  \
  X(greater, >, "gt")                               \
  X(less_equals, <=, "le")                          \
  X(greater_equals, >=, "ge")                       \
  X(spaceship, <=>, "ss")                           \
  X(ampersand_ampersand, &&, "aa")                  \
  X(pipe_pipe, ||, "oo")                            \
  X(less_less, <<, "ls")                            \
  X(greater_greater, >>, "rs")                      \
  X(less_less_equals, <<=, "lS")                    \
  X(greater_greater_equals, >>=, "rS")              \
  X(plus_plus, ++, "pp")                            \
  X(minus_minus, --, "mm")                          \
  X(comma, XYZ_REFLECTION_COMMA, "cm")

#define XYZ_REFLECTION_FOR_EACH_NULLARY_OPERATOR(X) \
  X(arrow, ->, "pt")                                \
  X(tilde, ~, "co")                                 \
  X(exclamation, !, "nt")

#define XYZ_REFLECTION_FOR_EACH_OPERATOR(X)   \
  XYZ_REFLECTION_FOR_EACH_GENERAL_OPERATOR(X) \
  XYZ_REFLECTION_FOR_EACH_NULLARY_OPERATOR(X)

#endif  // XYZ_REFLECTION_OPERATORS_H_
