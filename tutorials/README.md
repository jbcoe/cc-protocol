# Tutorials

Three standalone tutorials. Each is a single GoogleTest file whose sections build up one technique step by step.

- [`type_erasure.cc`](type_erasure.cc) — manual type erasure from first principles: a vtable of function pointers, and an eraser object built from an erased pointer plus a vtable pointer.
- [`vanishing_this_pointer.cc`](vanishing_this_pointer.cc) — a layout/casting technique for giving a data member ordinary call syntax, by recovering the address of its owning object from `this`.
- [`reflection.cc`](reflection.cc) — C++26 reflection, from `std::meta::info` up to synthesizing a type's members at compile time. Requires a P2996 compiler (`scripts/cmake.sh -DXYZ_PROTOCOL_BUILD_REFLECTION_TUTORIAL=ON`). Uses the vtable technique from `type_erasure.cc` and the call-syntax technique from `vanishing_this_pointer.cc` to generate that machinery at compile time instead of by hand.

Read `reflection.cc` last, since it builds on the other two; `type_erasure.cc` and `vanishing_this_pointer.cc` can be read in any order.
