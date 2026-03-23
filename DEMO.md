# Structural Subtyping in C++: Protocols

C++ developers have long relied on nominal subtyping (inheritance from abstract base classes) to define polymorphic interfaces. While powerful, inheritance tightly couples your components, forces reference semantics (`std::unique_ptr`), and makes it impossible to retroactively apply interfaces to third-party types.

Python solved a similar problem with **Protocols** (PEP 544), allowing for static "duck typing." If it walks like a duck and quacks like a duck, the type checker accepts it.

What if we could bring that same elegance to C++, combining structural subtyping with modern value semantics?

Here is a look at an experimental implementation of **C++ Protocols**.

## 1. Define Your Interface

Unlike traditional polymorphism, your interface is just a plain struct. No `virtual` keywords, no `= 0`, and no base classes.

```cpp
// interface_B.h
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

## 2. Write Your Implementation

Now, write your concrete type. Notice that `MyImplementation` does **not** inherit from `xyz::B`. It only needs to provide the methods defined in the interface.

```cpp
#include <vector>
#include <string>

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

## 3. Erase the Type with Value Semantics

We can now use `xyz::protocol_B`, an automatically generated type-erased wrapper. 

Unlike `std::function` (which only wraps a single callable) or `std::unique_ptr` (which requires dynamic allocation and pointer semantics), `protocol_B` acts like a regular value. It copies deeply, propagates `const` correctly, and supports custom allocators.

```cpp
#include "generated_protocol_B.h"

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

## 4. Meaningful Compiler Errors

Because the generated wrapper uses C++20 `requires` clauses, the compiler validates the structural match at compile-time. If you make a mistake—like returning the wrong type or forgetting a `const` qualifier—the compiler tells you exactly what went wrong.

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

## How It Works Under the Hood

To synthesize the type-erased wrapper and virtual dispatch boilerplate automatically, this project relies on AST parsing (via Clang and `py_cppmodel`). 

As C++ reflection (P2996) matures and code injection capabilities are added in future standards (C++29+), this entire generation process will move directly into the compiler, making `xyz::protocol<B>` natively achievable in standard C++.