#ifndef XYZ_PROTOCOL_INTERFACE_MUTATING_FUNCTION_H
#define XYZ_PROTOCOL_INTERFACE_MUTATING_FUNCTION_H

namespace xyz {

// A non-const callable function object interface.
//
// protocol<MutatingFunction> is equivalent to
// std::copyable_function<int(int)>.
// protocol_view<MutatingFunction> is equivalent to
// std::function_ref<int(int)>.
//
// Any type T satisfying: { t(int) } -> std::convertible_to<int> with a
// non-const overload can be stored in protocol<MutatingFunction>.
struct MutatingFunction {
  int operator()(int x);
};

}  // namespace xyz
#endif  // XYZ_PROTOCOL_INTERFACE_MUTATING_FUNCTION_H
