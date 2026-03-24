macro(xyz_generate_protocol)
  set(options "")
  set(oneValueArgs CLASS_NAME INTERFACE OUTPUT)
  set(multiValueArgs "")
  cmake_parse_arguments(XYZ_GEN "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  add_custom_command(
    OUTPUT ${XYZ_GEN_OUTPUT}
    COMMAND
      ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_protocol.py
      ${XYZ_GEN_INTERFACE}
      ${XYZ_GEN_OUTPUT} --class_name ${XYZ_GEN_CLASS_NAME}
      --template ${CMAKE_CURRENT_SOURCE_DIR}/scripts/protocol.j2 --compiler
      ${CMAKE_CXX_COMPILER}
    DEPENDS ${XYZ_GEN_INTERFACE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_protocol.py
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/protocol.j2
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_source_files_properties(
    ${XYZ_GEN_OUTPUT} PROPERTIES GENERATED
                               TRUE)
endmacro()
