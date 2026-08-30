# `protocol`: Structural Subtyping for C++

[![Language](https://img.shields.io/badge/language-C++-blue.svg)](https://isocpp.org/)
[![Standard](https://img.shields.io/badge/C%2B%2B-20%2B-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](https://opensource.org/licenses/MIT)

We propose the addition of two class templates, `protocol<T, A>` and
`protocol_view<T>`, to the C++ Standard Library. Both classes support
structural-subtyping, `protocol` is owning, `protocol_view` is non-owning.

See [DRAFT.md](DRAFT.md) for more details on design.

This repository contains both the ISO C++ proposal to add these new library
types and a reference implementation, `protocol.hh`, built on C++26 reflection
(P2996). It requires a compiler with reflection support, such as a GCC trunk
snapshot with `-freflection`.

## Standardization

The paper [P4148R2](https://wg21.link/p4148r2) (derived from
[DRAFT.md](DRAFT.md)) was presented to the C++ Standard Library Incubator
working group in Brno on June 11th 2026. The authors have been encouraged to
continue work.

## Contributing and Development

For build instructions, testing and contributing guidelines, see
[CONTRIBUTING.md](CONTRIBUTING.md); for how the implementation works, see
[implementation-notes.md](implementation-notes.md).

## GitHub codespaces

Press `.` or visit [https://github.dev/jbcoe/cc-protocol] to open the project in
an instant, cloud-based, development environment. We have defined a
[devcontainer](.devcontainer/devcontainer.json) that will automatically install
the dependencies required to build and test the project.
