
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
# GCC trunk's null, alignment and vptr sanitizer checks reject a `template for`
# over std::meta::info outside a consteval function (GCC PR127592). Drop this
# once a snapshot fixes it. A source property, unlike target_compile_options,
# follows the -fsanitize=undefined that the test target inherits.
set_source_files_properties(
    03_reflection.cc PROPERTIES COMPILE_OPTIONS
                                "-fno-sanitize=null,alignment,vptr")

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

xyz_add_test(
    NAME
    wide_pointer_sbo_tutorial
    FILES
    07_wide_pointer_sbo.cc)

xyz_add_test(
    NAME
    wide_pointer_narrowing_tutorial
    FILES
    08_wide_pointer_narrowing.cc)
