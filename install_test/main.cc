#include "protocol.hh"

struct Shape {
  double area() const;
};

struct Square {
  double side;

  double area() const { return side * side; }
};

int main() {
  xyz::reflection::protocol<Shape> shape(Square{2.0});
  return shape.area() == 4.0 ? 0 : 1;
}
