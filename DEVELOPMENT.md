# Developer Guide for C++ Protocol Library

This document outlines how to set up, build, and understand the C++ protocol library, a proof-of-concept implementation for structural subtyping using type erasure.

## Project Overview

The C++ protocol library is a code generation system designed to enable structural subtyping for C++. It allows C++ types to satisfy an interface based on their structure (i.e., the presence of conforming member functions) rather than explicit inheritance. This is achieved through type erasure, value semantics, and custom allocator awareness, aiming to provide a mechanism similar to Python's Protocols.

**Key Goals:**
*   Implement structural subtyping in C++.
*   Achieve type erasure for interfaces without requiring inheritance.
*   Maintain value semantics, const-correctness, and allocator awareness.

## Getting Started

### Prerequisites

Before building, ensure you have the following installed:
*   CMake
*   A modern C++ compiler (e.g., C++20 compliant)
*   `uv` (as indicated by `uv.lock` file, potentially used for Python dependencies)

### Building and Testing

The project uses CMake for its build system.

1.  **Build Script:** Navigate to the project root and run the provided build script:
    ```bash
    ./scripts/cmake.sh
    ```
    This script handles the entire build and test process.
2.  **Build Options:** For more detailed options and targets (e.g., specific build types, cleaning, testing), run:
    ```bash
    ./scripts/cmake.sh --help
    ```
    The script will configure, build, and run tests.

## Core Concepts

### Structural Subtyping

Unlike traditional nominal subtyping (where a type must explicitly inherit from another), structural subtyping considers two types equivalent if they have the same structure, typically meaning they support the same set of operations (methods). In this project, a type is considered to "implement" a protocol if it possesses all the member functions defined by that protocol, with compatible signatures.

### Type Erasure

Type erasure is a technique used to hide the specific type of an object while still allowing it to be manipulated through a common interface. In this library, a `protocol` wrapper internally uses type erasure to hold any object that structurally conforms to the defined interface, without the object needing to inherit from a common base class. This enables polymorphism without the constraints of traditional inheritance.

## Interface Definition

Interfaces for the `protocol` wrapper are defined in plain C++ header files. The code generation process parses these headers to determine the required methods.

### Supported Features (Current Status)

*   **Member Functions:** The system is primarily designed to support non-template member functions.
*   **Limitations:** The exact capabilities and limitations of `py_cppmodel` and `libclang` in parsing C++ features for interface definition are not fully documented. There isn't a definitive list of what works and what does not.
*   **Recommendation:** Developers should consult the test files, particularly `protocol_test.cc` and its associated interface headers, for concrete examples of supported interface patterns. Experimentation is encouraged.

## Code Generation

The library employs a code generation strategy to create the type-erased `protocol` wrappers.

### Process

1.  **Parsing:** The `scripts/generate_protocol.py` script uses `libclang` (via the `py_cppmodel` Python wrapper) to parse the Abstract Syntax Tree (AST) of user-defined C++ interface headers.
2.  **Template Rendering:** The parsed interface information is then fed into Jinja2 templates (e.g., `scripts/protocol.j2`) to generate the C++ code for the `protocol` wrapper.
3.  **Build Integration:** This code generation step is integrated into the CMake build process via the `xyz_generate_protocol` macro defined in `cmake/xyz_generate_protocol.cmake`.

### Using the `xyz_generate_protocol` Macro

The `xyz_generate_protocol` CMake macro automates the code generation.

*   **Location:** `cmake/xyz_generate_protocol.cmake`
*   **Inline Documentation:** This file contains inline documentation detailing the macro's parameters and usage.
*   **Typical Invocation:** A typical invocation in a `CMakeLists.txt` file would look something like:
    ```cmake
    # Example snippet (conceptual, refer to the actual file for exact parameters)
    xyz_generate_protocol(
        INTERFACE_HEADER "path/to/your/Interface.h"
        OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated"
        # ... other parameters
    )
    ```
    This macro ensures that the Python script is run during the CMake configuration or build phase to generate the necessary C++ source files for the protocol.

## Usage Examples

The primary location to find examples of how to use the `protocol` library is within the test suite.

*   **`protocol_test.cc`:** This file, while large, contains early tests that serve as excellent examples of the library's intended usage. It demonstrates how to define an interface, generate the protocol, and instantiate/use the wrapper with concrete types. Developers are encouraged to explore this file for practical demonstrations.

## Current Status and Limitations

**This library is a fast-moving, working proof of concept.**

*   **Not for Production:** It is **not** set up for adoption in other projects.
*   **Unknown Limitations:** It has unknown limitations and is prone to breaking changes.
*   **Development Status:** Use it for understanding the concepts and contributing to its development, but avoid using it in production code.

## Contributing and Issue Tracking

*   **Issues:** All issues, bugs, and feature requests should be tracked on the project's GitHub repository: [https://github.com/jbcoe/cc-protocol/issues](https://github.com/jbcoe/cc-protocol/issues)

---
*Last updated: March 25, 2026*
