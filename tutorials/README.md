# Tutorials

Three standalone tutorials. Each is a single GoogleTest file whose sections build up one technique step by step.

- [`1_type_erasure.cc`](1_type_erasure.cc) — manual type erasure from first principles: a vtable of function pointers, and an eraser object built from an erased pointer plus a vtable pointer.
- [`2_vanishing_this_pointer.cc`](2_vanishing_this_pointer.cc) — a layout/casting technique for giving a data member ordinary call syntax, by recovering the address of its owning object from `this`.
- [`3_reflection.cc`](3_reflection.cc) — C++26 reflection, from `std::meta::info` up to synthesizing a type's members at compile time. Requires a P2996 compiler (`scripts/cmake.sh -DXYZ_PROTOCOL_BUILD_REFLECTION_TUTORIAL=ON`).
