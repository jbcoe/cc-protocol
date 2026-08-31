
xyz_add_test(
    NAME
    polymorphism_tutorial
    FILES
    polymorphism.cc)

xyz_add_test(
    NAME
    vanishing_this_tutorial
    FILES
    vanishing_this.cc)

xyz_add_test(
    NAME
    type_erasure_tutorial
    FILES
    type_erasure.cc)

xyz_add_test(
    NAME
    allocators_tutorial
    FILES
    allocators.cc)

xyz_add_test(
    NAME
    reflection_tutorial
    LINK_LIBRARIES
    xyz_protocol::protocol
    FILES
    reflection.cc)

xyz_add_test(
    NAME
    advanced_vtables_tutorial
    LINK_LIBRARIES
    xyz_protocol::protocol
    FILES
    advanced_vtables.cc)
