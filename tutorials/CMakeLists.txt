
xyz_add_test(
    NAME
    polymorphism_tutorial
    FILES
    polymorphism.cc)


if(XYZ_PROTOCOL_BUILD_REFLECTION)
xyz_add_test(
    NAME
    vanishing_this_tutorial
    VERSION
    26
    FILES
    vanishing_this.cc)
endif()

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
