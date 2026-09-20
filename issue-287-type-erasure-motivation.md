# Issue #287: Draft a paper on why C++ needs a type erasing wrapper

Offline copy of <https://github.com/jbcoe/cc-protocol/issues/287>, taken
2026-09-20.

Opened by @jbcoe on 2026-09-02. State: open. Labels: Important.

## Issue text

A self-contained document on motivation for type-erasing wrappers would be useful to frame discussion for protocol and for proxy. We should look into collaboration with @mingxwa to produce this design-independent paper.

The C++ committee forbids the use of AI in producing papers so this must be human written. Presumably we can use AI to help with proofreading.

## Comment by @philipcraig, 2026-09-20

### Random paper thoughts:

1. Can it be a joint paper with Proxy (P3086)? Points for or not
1. Motivation on why in the Standard not just a library (various polls)
 - erased wrapper (protocol) is an interface type. For two disjoint libraries to use it at their boundaries, needs to be standard
 - standard library itself is a big customer (`function`, `shared_ptr`'s deleter, `any`, `pmr::memory_resource`, `format_args`' handle, `move_only_function`, `copyable_function`, `function_ref`, `polymorphic`
 - every time it's used but not as a standard thing, the design and implementation have to re-decide upon the semantics of moved-from, null, small-buffer and allocators
 - non-standard implementations (almost?) always skip allocators as the benefits don't justify the individual effort

### Use cases

1. callable-wrapper family
2. Heterogeneous collections of values - the usual `vector` of drawables
 - something more specific than just polymorphism don't want a base class as don't want pointer semantics
 - Sean Parent's Photoshop-style document model
 - `polymorphic<T>` gives value semantics but needs a base class
3. Interfaces added afterwards to types you don't own
 - Go allows interfaces after the fact
 - Python has PEP 544 for the same use-case
 - C++ already has this property at compile-time with concepts, maybe frame this as run-time equivalent?
 4. Open extension points without a base class
 - LLVM's `PassConcept` and `PassModel` classes, also credited to Parent's talk
 5. Compile-time ABI firewalls for generic code
 - a generic library might need a nameable non-template type at its boundary
 6. a non-owning view as a function parameter
 7. ??? not sure if we can achieve in the spec/design ??? constrained targets like embedded or no heap or no RTTI?

Maybe we need performance evidence? Of what though? compile time? run-time? space? there is such evidence in Proxy paper
