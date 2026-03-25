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

Unlike traditional polymorphism, the interface is just a struct. No
`virtual` keywords, no `= 0`, and no base classes.

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
```

We can now use `xyz::protocol_B`, an automatically generated type-erased
wrapper. It copies deeply, propagates `const` correctly, and supports custom
allocators.

```cpp
#include "interface_B.h"
#include "generated/protocol_B.h"

void run_pipeline(xyz::protocol_B<> worker) {
  if (!worker.is_ready()) {
    worker.process("hello protocols");
  }

  for (int result : worker.get_results()) {
    // ...
  }
}

int main() {
  // Construct the protocol in-place with our implementation
  xyz::protocol_B<> p(std::in_place_type<MyImplementation>);

  run_pipeline(p); // Pass by value!
  return 0;
}
```

The generated wrapper uses C++20 concepts and `requires` clauses: any
structural mismatch emits clear, pinpointed compile-time errors rather than
deeply nested template instantiation failures.

## Dependencies

The code generation script requires Python 3.12+ and `uv`.

```bash
uv sync
```

## References

- PEP 544: [Protocols: Structural subtyping (static duck typing)](https://peps.python.org/pep-0544/)

- P3019: [std::indirect and std::polymorphic](https://isocpp.org/files/papers/P3019R4.html)

- P2996: [Reflection for C++26](https://isocpp.org/files/papers/P2996R4.html)

- py_cppmodel: [Python wrappers for clang's parsing of C++](https://github.com/jbcoe/py_cppmodel)
