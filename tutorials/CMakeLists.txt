
xyz_add_test(
    NAME
    polymorphism_tutorial
    FILES
    polymorphism.cc)

xyz_add_test(
    NAME
    narrowing_conversions_tutorial
    FILES
    narrowing_conversions.cc)

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

if(XYZ_PROTOCOL_BUILD_REFLECTION)
    xyz_add_test(
        NAME
        reflection_tutorial
        VERSION
        26
        FILES
        reflection.cc)
    target_compile_options(reflection_tutorial PRIVATE -freflection)
endif()
