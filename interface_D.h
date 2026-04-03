#ifndef XYZ_PROTOCOL_INTERFACE_D_H
#define XYZ_PROTOCOL_INTERFACE_D_H

#include <compare>

namespace xyz {

struct D {
  int operator+(int x) const;
  int operator-(int x) const;
  int operator*(int x) const;
  int operator/(int x) const;
  int operator%(int x) const;
  int operator^(int x) const;
  int operator&(int x) const;
  int operator|(int x) const;
  int operator~() const;
  bool operator!() const;
  void operator=(int x);
  bool operator<(int x) const;
  bool operator>(int x) const;
  void operator+=(int x);
  void operator-=(int x);
  void operator*=(int x);
  void operator/=(int x);
  void operator%=(int x);
  void operator^=(int x);
  void operator&=(int x);
  void operator|=(int x);
  int operator<<(int x) const;
  int operator>>(int x) const;
  void operator<<=(int x);
  void operator>>=(int x);
  bool operator==(int x) const;
  bool operator!=(int x) const;
  bool operator<=(int x) const;
  bool operator>=(int x) const;
  std::strong_ordering operator<=>(int x) const;
  bool operator&&(bool x) const;
  bool operator||(bool x) const;
  void operator++();
  void operator--();
  int operator,(int x) const;
  int operator->*(int x) const;
  int* operator->();
  int operator()();
  int operator[](int x) const;
};

}  // namespace xyz
#endif  // XYZ_PROTOCOL_INTERFACE_D_H
