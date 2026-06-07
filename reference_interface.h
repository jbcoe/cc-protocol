#ifndef XYZ_PROTOCOL_REFERENCE_INTERFACE_H
#define XYZ_PROTOCOL_REFERENCE_INTERFACE_H

#include <compare>
#include <cstddef>
#include <string_view>

namespace xyz {

struct ReferencePoint {
  double x;
  double y;
};

struct ReferenceInterface {
  // 1. Basic member functions and parameter types
  int get_value() const;
  void update(const ReferencePoint& point, int* status);

  // 2. Exception specifications (noexcept)
  double compute(double input) noexcept;

  // 4. Overloaded functions
  void overloaded(int value);
  void overloaded(int value) const;
  void overloaded(std::string_view value) const;

  // 5. Operator overloads
  void operator+=(int value);
  int operator()(int x, int y) const;
  int operator[](std::size_t index);
};

}  // namespace xyz
#endif  // XYZ_PROTOCOL_REFERENCE_INTERFACE_H
