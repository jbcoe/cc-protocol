
#include <utility>

#include "generated/protocol_A.h"

#ifdef TEST_MISSING_METHOD
class BadALike_MissingMethod {
 public:
  int count() { return 42; }
};

void test() {
  xyz::protocol<xyz::A> a(std::in_place_type<BadALike_MissingMethod>);
}
#endif

#ifdef TEST_WRONG_RETURN_TYPE
class BadALike_WrongReturnType {
 public:
  std::string_view name() const { return "name"; }

  std::string count() { return "42"; }  // not convertible to int
};

void test() {
  xyz::protocol<xyz::A> a(std::in_place_type<BadALike_WrongReturnType>);
}
#endif

#ifdef TEST_PRIMARY_TEMPLATE_INSTANTIATION
struct NoSpecialization {};

void test() { xyz::protocol<NoSpecialization> p; }
#endif
