# Developer Guide for C++ Protocol Library

This document explains how to set up, build, and understand the internals of the C++ protocol library. For a high-level overview of structural subtyping, library design principles, and code examples, please refer to the [README](README.md).

## Getting Started

### Prerequisites

Before building, ensure you have [CMake](https://cmake.org/download/) 3.25 or
later, a GCC with C++26 reflection (P2996) support, and
[uv](https://docs.astral.sh/uv/getting-started/installation/) installed. The
reflection compiler is either the GCC trunk snapshot from
[jwakely.github.io/pkg-gcc-latest](https://jwakely.github.io/pkg-gcc-latest/)
or Ubuntu 26.04's `gcc-16` package. The project relies on `uv` to manage
Python dependencies and execute build scripts.

### Building and Testing

The project uses CMake for its build system.

1. Build Script: To build the project and run tests, navigate to the
   project root directory and execute the provided build script:

```bash
./scripts/cmake.sh --reflection
```

This script manages the entire build and test process.

2.Build Options: For more detailed options, such as build types or
   cleaning targets, run:

```bash
./scripts/cmake.sh --help
```

The script will configure, build, and execute tests.

### Static Analysis

CI runs clang-tidy on every translation unit and fails on any finding
(`WarningsAsErrors: '*'` in `.clang-tidy`). The `clang-tidy` job is a required
status check for merging to `main`; on pull requests that touch no C++ or
CMake sources the job is skipped, which counts as passing. To reproduce
locally, with
`clang-tidy` on your `PATH` (the devcontainer installs `clang-tidy-22`):

```bash
./scripts/cmake.sh --debug --clang-tidy
```

The same run is available as an opt-in pre-commit hook. It is a full build, so
it is not part of the default commit-time hooks:

```bash
uv run pre-commit run --hook-stage manual clang-tidy
```

CI pins clang-tidy 22 (`COMPILER_VERSION` in `.github/workflows/clang-tidy.yml`);
the check set differs between releases, so a different local version may
report different findings. CMake warns at configure time when the version it
found does not match. The pinned version is recorded in three places that must
be updated together: the workflow, `docker/Dockerfile`, and
`ClangTidy_EXPECTED_MAJOR_VERSION` in `cmake/modules/FindClangTidy.cmake`.

Fix genuine findings. Suppress false positives at the site with a `NOLINT`
comment that names the check and gives a reason, or, for a check that does not
apply to a whole directory, in that directory's `.clang-tidy` (see
`tutorials/.clang-tidy`). Checks disabled for the whole repository are listed
with their rationale at the top of `.clang-tidy`.

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

Interfaces for `protocol` and `protocol_view` are plain structs or classes.
Their public, non-virtual, non-template member functions define the protocol;
`protocol.hh` inspects them with C++26 reflection at compile time.

### Supported Features

- Member Functions: overloaded member functions (distinguished by parameter
  types or arity), `operator()` including overloaded call operators, const
  and non-const overloads of the same member, `noexcept` member functions,
  and static member functions on the conforming type (a static candidate has
  no object parameter, so it can satisfy any const or reference qualification
  of the interface member). Static member functions declared on the interface
  itself are ignored.

- Limitations: No operators other than `operator()` are supported. Member
  function templates are not matched, since only non-template functions are
  considered as candidates. Conformance checking accounts for an interface
  member's lvalue/rvalue reference qualifier, but the generated call wrapper
  does not itself apply the qualifier. There is no conversion between a
  `protocol`/`protocol_view` of one interface and another. Conformance
  checking is O(N\*M) in the number of interface and candidate member
  functions.

- Guidance: Developers should refer to `protocol_test.cc`,
  `forwarding_test.cc`, `allocator_tests.cc` and `tutorials/reflection.cc` for
  examples of supported interface patterns.

## Usage Examples

Examples of how to use the `protocol` library can be found within the test
suite.

- `protocol_test.cc`: This file contains tests that demonstrate the library's
  intended usage. It shows how to define an interface and then instantiate
  and use the wrapper with concrete types. Developers can examine this file
  for practical examples.

## Current Status and Limitations

This library is an active proof of concept and is subject to change.

- Not for Production: It is not set up for use in other projects.

- Limitations: It has unknown limitations and may experience breaking
  changes.

- Development: Use this library for understanding its concepts and
  contributing to its development. Avoid using it in production code.

## AI Coding Sandboxes

The repository includes a Docker-based sandbox script for AI coding
assistants. It mounts the project into a container with all build dependencies
pre-installed, providing an isolated environment for AI-assisted development.

### Usage

```bash
./scripts/agentic-sandbox.sh <agent> [options]
```

where `<agent>` is either `claude` or `gemini`.

### Options

| Flag | Description |
|------|-------------|
| `--rebuild-docker` | Rebuild the Docker image before starting. |
| `--update` | Update the agent CLI to the latest version before running. |
| `-v`, `--verbose` | Enable verbose logging. |

### Using pre-commit Locally to run Github Workflow checks

Install pre-commit hooks into your local repository:

```bash
uv run pre-commit install
```

Run all hooks against every file:

```bash
uv run pre-commit run --all-files
```

## Contributing and Issue Tracking

- Issues: All issues, bugs, and feature requests should be tracked on the
  project's GitHub repository:
  [https://github.com/jbcoe/cc-protocol/issues](https://github.com/jbcoe/cc-protocol/issues)

---

_Last updated: August 30, 2026_
