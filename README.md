# `protocol`: Structural Subtyping for C++

[![Language](https://img.shields.io/badge/language-C++-blue.svg)](https://isocpp.org/)
[![Standard](https://img.shields.io/badge/C%2B%2B-20%2B-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](https://opensource.org/licenses/MIT)

An experimental C++ library and code generation tool for static structural
subtyping (duck typing) with value semantics.

## Overview

Polymorphic interfaces in C++ traditionally require explicit inheritance from an
abstract base class. This nominal subtyping tightly couples independent
components, makes it impossible to retroactively apply interfaces to third-party
types, and typically forces reference semantics (e.g., `std::unique_ptr`).

Inspired by Python's `Protocol` (PEP 544), this repository explores bringing a
similar paradigm to C++. By using AST parsing (via Clang) and code generation,
the tool synthesizes type-erased wrappers that accept any type structurally
conforming to an interface — without inheritance.

These protocols maintain deep-copy value semantics, strict `const`-propagation,
and allocator awareness, consistent with the design of `jbcoe/value_types`
(P3019).

## Standardization

As C++ reflection (P2996) matures and code injection is added in future
standards (C++29+), the generation approach demonstrated here via `py_cppmodel`
will be achievable natively within the language.

A draft proposal is available in `proposals/DRAFT.md`.

## Use

The interface is a plain struct — no `virtual` keywords, no `= 0`, no base
classes.

```cpp
#pragma once
#include <string>
#include <vector>

namespace xyz {

struct B {
  void process(const std::string& input);
  std::vector<int> get_results() const;
  bool is_ready() const;
};

}  // namespace xyz
```

Write your concrete type. It does not need to inherit from `xyz::B` — it only
needs to structurally provide the methods defined in the interface.

```cpp
namespace xyz {

class MyImplementation {
  std::vector<int> results_;
  bool ready_ = false;

 public:
  // Structurally matches xyz::B
  void process(const std::string& input) {
    results_.push_back(input.length());
    ready_ = true;
  }
  std::vector<int> get_results() const { return results_; }
  bool is_ready() const { return ready_; }
};

}  // namespace xyz
```

`xyz::protocol<xyz::B>` is an automatically generated type-erased wrapper. It
copies deeply, propagates `const` correctly, and supports custom allocators.

```cpp
#include "generated/protocol_B.h"

void run_pipeline(xyz::protocol<xyz::B> worker) {
  if (!worker.is_ready()) {
    worker.process("hello protocols");
  }

  for (int result : worker.get_results()) {
    // ...
  }
}

int main() {
  // Construct the protocol in-place with our implementation
  xyz::protocol<xyz::B> p(std::in_place_type<xyz::MyImplementation>);

  run_pipeline(p); // Pass by value!
  return 0;
}
```

The generated wrapper uses C++20 concepts and `requires` clauses: any structural
mismatch produces clear, pinpointed compile-time errors rather than deeply nested
template instantiation failures.

```cpp
class BadImplementation {
 public:
  void process(const std::string& input);
  // ERROR: Missing get_results()
  // ERROR: is_ready() is missing 'const'
  bool is_ready();
};

// COMPILER ERROR:
// constraints not satisfied
// the required expression 'std::as_const(t).is_ready()' is invalid
```

## `protocol_view`: Non-Owning Structural Subtyping

Alongside `protocol`, the code generator also produces a `protocol_view`
specialization. While `protocol` manages the lifecycle of the underlying object
(with deep-copy value semantics), `protocol_view` is a lightweight, non-owning
reference — analogous to `std::string_view` or `std::span`, but for protocols.

```cpp
// `view` observes the object without owning or copying it.
void inspect(xyz::protocol_view<xyz::B> view) {
  if (view.is_ready()) {
    // ...
  }
}

int main() {
  xyz::MyImplementation impl;

  // Implicitly constructs a view over `impl` since it fulfills the structural requirements.
  inspect(impl);

  xyz::protocol<xyz::B> p(std::in_place_type<xyz::MyImplementation>);
  // Implicitly constructs a view over `p` as the protocol itself satisfies the requirements.
  inspect(p);

  return 0;
}
```

`protocol_view` enables zero-overhead structural dispatch at function
boundaries, avoiding allocations and deep copies entirely.

## Implementation Details and Benchmarks

The code generator supports two dispatch strategies:

1. **Virtual Dispatch (Default)**: Generates a traditional C++ polymorphic class
   hierarchy with `virtual` methods. The type-erased wrapper heap-allocates a
   control block derived from a common interface.

2. **Manual Vtables**: Generates a struct-of-function-pointers representing the
   vtable, managing type-erasure and dispatch via pointer indirection.

Both implementations enforce identical constraints (value semantics, `const`
correctness, and custom allocators). The library builds both versions to verify
equivalence and provides a `protocol_benchmark` target for directly comparing
their performance across allocations, copies, moves, and member function calls.

```bash
# Build and run the benchmark comparing the two implementations
./scripts/cmake.sh benchmark
```

## Contributing and Development

For build instructions, testing, contributing guidelines, and a deeper look into
the code generation architecture, see the [Developer Guide](CONTRIBUTING.md).

## References

- PEP 544: [Protocols: Structural subtyping (static duck
  typing)](https://peps.python.org/pep-0544/)

- P3019: [std::indirect and
  std::polymorphic](https://isocpp.org/files/papers/P3019R14.pdf)

- P2996: [Reflection for C++26](https://isocpp.org/files/papers/P2996R13.html)

- py_cppmodel: [Python wrappers for clang's parsing of
  C++](https://github.com/jbcoe/py_cppmodel)
