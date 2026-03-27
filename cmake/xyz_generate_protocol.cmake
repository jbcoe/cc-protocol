include_guard(GLOBAL)

#[=======================================================================[.rst:
xyz_generate_protocol
------------------

Overview
^^^^^^^^
Generates a protocol implementation based on a provided interface header using
a Jinja2 template and a Python generation script.

.. code-block:: cmake

  xyz_generate_protocol(
      [CLASS_NAME <name>]
      [INTERFACE <header_file>]
      [OUTPUT <output_file>]
      [HEADER <include_header>]
      [MANUAL_VTABLE]
  )
   -- Configures a custom command to generate protocol source files.

  ``CLASS_NAME``
    The name of the class to be generated.

  ``INTERFACE``
    The input interface header file defining the protocol's structure.

  ``OUTPUT``
    The path to the generated output source file.

  ``HEADER``
    The header file to be included in the generated source file.

  ``MANUAL_VTABLE``
    If specified, uses the manual vtable template for generation instead of the
    default.

#]=======================================================================]
macro(xyz_generate_protocol)
  set(options MANUAL_VTABLE)
  set(oneValueArgs CLASS_NAME INTERFACE OUTPUT HEADER)
  set(multiValueArgs "")
  cmake_parse_arguments(XYZ_GENERATE "${options}" "${oneValueArgs}"
                        "${multiValueArgs}" ${ARGN})

  if(XYZ_GENERATE_MANUAL_VTABLE)
    set(TEMPLATE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/scripts/protocol_manual_vtable.j2)
  else()
    set(TEMPLATE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/scripts/protocol.j2)
  endif()

  get_filename_component(XYZ_GENERATE_OUTPUT_DIR "${XYZ_GENERATE_OUTPUT}" DIRECTORY)
  add_custom_command(
    OUTPUT ${XYZ_GENERATE_OUTPUT}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${XYZ_GENERATE_OUTPUT_DIR}"
    COMMAND
      ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_protocol.py
      ${XYZ_GENERATE_INTERFACE} ${XYZ_GENERATE_OUTPUT} --class_name ${XYZ_GENERATE_CLASS_NAME}
      --template ${TEMPLATE_FILE} --compiler
      ${CMAKE_CXX_COMPILER} --header ${XYZ_GENERATE_HEADER}
    DEPENDS ${XYZ_GENERATE_INTERFACE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_protocol.py
            ${TEMPLATE_FILE}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  set_source_files_properties(${XYZ_GENERATE_OUTPUT} PROPERTIES GENERATED TRUE)
endmacro()
