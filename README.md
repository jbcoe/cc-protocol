# `protocol`: Structural Subtyping for C++

[![Language](https://img.shields.io/badge/language-C++-blue.svg)](https://isocpp.org/)
[![Standard](https://img.shields.io/badge/C%2B%2B-20%2B-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](https://opensource.org/licenses/MIT)

We propose the addition of two class templates, `protocol<T, A>` and
`protocol_view<T>`, to the C++ Standard Library. Both classes support
structural-subtyping, `protocol` is owning, `protocol_view` is non-owning.

See [DRAFT.md](DRAFT.md) for more details on design.

This repository contains both the ISO C++ proposal to add these new library
types and a reference implementation. The reference implementation is currently
reliant on a Python code-generation step as C++26 reflection is missing some of
the features needed to generate code needed by these types at compile time.

## Standardization

The paper [P4148R2](https://wg21.link/P4148R2.pdf) (derived from
[DRAFT.md](DRAFT.md)) was presented to the C++ Standard Library Incubator
working group in Brno on June 11th 2026. The authors have been encouraged to
continue work.

## Contributing and Development

For build instructions, testing, contributing guidelines, and a deeper look into
the code generation architecture, see [CONTRIBUTING.md](CONTRIBUTING.md).

## GitHub codespaces

Press `.` or visit [https://github.dev/jbcoe/cc-protocol] to open the project in
an instant, cloud-based, development environment. We have defined a
[devcontainer](.devcontainer/devcontainer.json) that will automatically install
the dependencies required to build and test the project.
