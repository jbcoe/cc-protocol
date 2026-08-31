
xyz_add_test(
    NAME
    polymorphism_tutorial
    FILES
    01_polymorphism.cc)

xyz_add_test(
    NAME
    type_erasure_tutorial
    FILES
    02_type_erasure.cc)

xyz_add_test(
    NAME
    reflection_tutorial
    LINK_LIBRARIES
    xyz_protocol::protocol
    FILES
    03_reflection.cc)

xyz_add_test(
    NAME
    vanishing_this_tutorial
    FILES
    04_vanishing_this.cc)

xyz_add_test(
    NAME
    allocators_tutorial
    FILES
    05_allocators.cc)

xyz_add_test(
    NAME
    advanced_vtables_tutorial
    LINK_LIBRARIES
    xyz_protocol::protocol
    FILES
    06_advanced_vtables.cc)
