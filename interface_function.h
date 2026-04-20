#ifndef XYZ_PROTOCOL_INTERFACE_FUNCTION_H
#define XYZ_PROTOCOL_INTERFACE_FUNCTION_H

namespace xyz {

// A const-callable function object interface.
//
// protocol<Function> is equivalent to std::copyable_function<int(int) const>.
// protocol_view<const Function> is equivalent to std::function_ref<int(int) const>.
//
// Any type T satisfying: { t(int) } -> std::convertible_to<int> with a const
// overload can be stored in protocol<Function>.
struct Function {
  int operator()(int x) const;
};

}  // namespace xyz
#endif  // XYZ_PROTOCOL_INTERFACE_FUNCTION_H
