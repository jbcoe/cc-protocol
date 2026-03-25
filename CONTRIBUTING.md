# Developer Guide for C++ Protocol Library

This document explains how to set up, build, and understand the internals of the C++ protocol library. For a high-level overview of structural subtyping, library design principles, and code examples, please refer to the [README](README.md).

## Getting Started

### Prerequisites

Before building, ensure you have [CMake](https://cmake.org/download/), a modern
C++ compiler (supporting C++20), and
[uv](https://docs.astral.sh/uv/getting-started/installation/) installed. The
project relies on `uv` to manage Python dependencies and execute build scripts.

### Building and Testing

The project uses CMake for its build system.

1. Build Script: To build the project and run tests, navigate to the
   project root directory and execute the provided build script:

```bash
./scripts/cmake.sh
```

This script manages the entire build and test process.

2.Build Options: For more detailed options, such as build types or
   cleaning targets, run:

```bash
./scripts/cmake.sh --help
```

The script will configure, build, and execute tests.

## Core Concepts

### Structural Subtyping

Traditional nominal subtyping requires a type to explicitly inherit from
another. Structural subtyping, in contrast, considers two types equivalent if
they have the same structure, typically meaning they support the same set of
operations. In this project, a type implements a protocol if it provides all the
member functions defined by that protocol with compatible signatures.

### Type Erasure

Type erasure is a technique that hides the specific type of an object, allowing
it to be manipulated through a common interface. The `protocol` wrapper in this
library uses type erasure internally. It holds any object that structurally
conforms to the defined interface without requiring that object to inherit from
a common base class, thus enabling polymorphism without traditional inheritance
constraints.

## Interface Definition

Interfaces for the `protocol` wrapper are defined in standard C++ header files.
The code generation process reads these headers to identify the required
methods.

### Supported Features

- Member Functions: The system is designed to support non-template member
  functions.

- Limitations: The exact capabilities of `py_cppmodel` and `libclang` for
  parsing C++ features in interface definitions are not fully documented. A
  definitive list of supported and unsupported features is not available.

- Guidance: Developers should refer to the test files, especially
  `protocol_test.cc` and its associated headers, for examples of supported
  interface patterns.

## Code Generation

The library uses a code generation strategy to create the `protocol` wrappers.

### Process

1. Parsing: The `scripts/generate_protocol.py` script uses `libclang` (with
   the `py_cppmodel` Python wrapper) to parse the Abstract Syntax Tree (AST) of
   user-defined C++ interface headers.

2. Template Rendering: The parsed interface information is then used with
   Jinja2 templates (like `scripts/protocol.j2`) to generate the C++ code for
   the `protocol` wrapper.

3. Build Integration: This code generation is integrated into the CMake
   build process using the `xyz_generate_protocol` macro found in
   `cmake/xyz_generate_protocol.cmake`.

### Using the `xyz_generate_protocol` Macro

The `xyz_generate_protocol` CMake macro automates code generation.

- Location: The macro is defined in `cmake/xyz_generate_protocol.cmake`.
- Documentation: This file contains inline documentation that details the
  macro's parameters and usage.

- Invocation Example: A typical invocation within a `CMakeLists.txt` file
  might look like this:

  ```cmake
  # Example snippet (refer to the actual file for exact parameters)
  xyz_generate_protocol(
      INTERFACE_HEADER "path/to/your/Interface.h"
      OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated"
      # ... other parameters
  )
  ```

  This macro ensures that the Python script runs during the CMake configuration
  or build phase to generate the necessary C++ source files for the protocol.

## Usage Examples

Examples of how to use the `protocol` library can be found within the test
suite.

- `protocol_test.cc`: This file contains tests that demonstrate the library's
  intended usage. It shows how to define an interface, generate the protocol,
  and then instantiate and use the wrapper with concrete types. Developers can
  examine this file for practical examples.

## Current Status and Limitations

This library is an active proof of concept and is subject to change.

- Not for Production: It is not set up for use in other projects.

- Limitations: It has unknown limitations and may experience breaking
  changes.

- Development: Use this library for understanding its concepts and
  contributing to its development. Avoid using it in production code.

## Contributing and Issue Tracking

- Issues: All issues, bugs, and feature requests should be tracked on the
  project's GitHub repository:
  [https://github.com/jbcoe/cc-protocol/issues](https://github.com/jbcoe/cc-protocol/issues)

---

_Last updated: March 25, 2026_
