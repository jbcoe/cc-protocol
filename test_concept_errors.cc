// The order of these includes is (currently) important to ensure the generated
// headers are included after the interfaces.
// clang-format off
#include "interface_A.h"
#include "generated_protocol_A.h"
# clang-format on

#ifdef TEST_MISSING_METHOD
class BadALike_MissingMethod {
 public:
  int count() { return 42; }
};

void test() { xyz::protocol_A<> a(std::in_place_type<BadALike_MissingMethod>); }
#endif

#ifdef TEST_WRONG_RETURN_TYPE
class BadALike_WrongReturnType {
 public:
  std::string_view name() const { return "name"; }

  double count() { return 42.0; }  // wrong return type
};

void test() {
  xyz::protocol_A<> a(std::in_place_type<BadALike_WrongReturnType>);
}
#endif

#ifdef TEST_MISSING_CONST
class BadALike_MissingConst {
 public:
  std::string_view name() { return "name"; }  // missing const

  int count() { return 42; }
};

void test() { xyz::protocol_A<> a(std::in_place_type<BadALike_MissingConst>); }
#endif
