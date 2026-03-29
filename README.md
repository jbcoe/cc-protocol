# `protocol`: Structural Subtyping for C++

[![Language](https://img.shields.io/badge/language-C++-blue.svg)](https://isocpp.org/)
[![Standard](https://img.shields.io/badge/C%2B%2B-20%2B-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](https://opensource.org/licenses/MIT)

An experimental C++ library and generation tool for enabling static structural
subtyping (duck typing) with value semantics.

## Overview

Polymorphic interfaces in C++ traditionally require explicit inheritance from an
abstract base class. This nominal subtyping tightly couples independent
components, makes it impossible to retroactively apply interfaces to third-party
types, and typically forces reference semantics (e.g., `std::unique_ptr`).

Inspired by Python's `Protocol` (PEP 544), this repository explores bringing a
similar paradigm to C++. By utilizing AST parsing (via Clang) and code
generation, we can automatically synthesize type-erased wrappers that accept any
type structurally conforming to an interface.

Crucially, these protocols maintain deep-copy value semantics, strict
`const`-propagation, and allocator awareness, aligning heavily with the
principles of `jbcoe/value_types` (P3019).

## Standardization

As C++ reflection (P2996) matures and advanced code injection capabilities are
added in future standards (C++29+), the generation process demonstrated here via
`py_cppmodel` will be achievable natively within the language.

A draft proposal detailing this feature can be found in `proposals/DRAFT.md`.

## Use

Unlike traditional polymorphism, the interface is just a struct. No `virtual`
keywords, no `= 0`, and no base classes.

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

Write your concrete type. It does **not** need to inherit from `xyz::B`. It only
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

We can now use `xyz::protocol<xyz::B>`, an automatically generated type-erased
wrapper. It copies deeply, propagates `const` correctly, and supports custom
allocators.

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
mismatch emits clear, pinpointed compile-time errors rather than deeply nested
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
(with deep-copy value semantics), `protocol_view` provides a lightweight,
non-owning reference. It functions similarly to `std::string_view` or
`std::span` but for protocols.

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
  // Implicitly constructs a view over `p` as the protocol itself satisfies the requirements!
  inspect(p);

  return 0;
}
```

A `protocol_view` provides true zero-overhead duck-typing at function
boundaries, decoupling types while avoiding the cost of allocations and deep
copies.

## Implementation Details and Benchmarks

The `protocol` code generator supports two different underlying dispatch
strategies:

1. Virtual Dispatch (Default): Generates a traditional C++ polymorphic class
   hierarchy with `virtual` methods. The type-erased wrapper heap-allocates a
   control block derived from a common interface.

2. Explicit Manual Vtables: Generates a struct-of-function-pointers representing
   the vtable. This approach manually manages type-erasure and dispatch via
   pointer indirection.

Both implementations enforce identical constraints (value semantics, `const`
correctness, and custom allocators). The library builds both versions to ensure
they are strictly equivalent and offers a `protocol_benchmark` target to
directly compare their performance for allocations, copies, moves, and member
function calls.

```bash
# Build and run the benchmark comparing the two implementations
./scripts/cmake.sh benchmark
```

## Contributing and Development

For instructions on how to build, test, and contribute to this project, as well
as a deeper look into the code generation architecture, please refer to the
[Developer Guide](CONTRIBUTING.md).

## References

- PEP 544: [Protocols: Structural subtyping (static duck
  typing)](https://peps.python.org/pep-0544/)

- P3019: [std::indirect and
  std::polymorphic](https://isocpp.org/files/papers/P3019R14.pdf)

- P2996: [Reflection for C++26](https://isocpp.org/files/papers/P2996R13.html)

- py_cppmodel: [Python wrappers for clang's parsing of
  C++](https://github.com/jbcoe/py_cppmodel)
