include_guard(GLOBAL)

#[=======================================================================[.rst:
xyz_fetchcontent_declare_cached
------------------

Overview
^^^^^^^^

Declares a FetchContent git dependency, backed by a persistent, shared
checkout under XYZ_CMAKE_FETCHCONTENT_CACHE_DIR when that variable names a
directory, so a fresh build directory reuses the checkout instead of
re-cloning. Each build tree still compiles its own copy of the dependency:
only the source is shared. Falls back to FetchContent's own
per-build-directory default when the variable is unset.

.. code-block:: cmake

  xyz_fetchcontent_declare_cached(
      NAME <name>
      GIT_REPOSITORY <url>
      GIT_TAG <tag>
      [SYSTEM]
  )

  ``NAME``
    The dependency's FetchContent name, as passed to
    FetchContent_MakeAvailable.

  ``GIT_REPOSITORY``
    The git URL to clone.

  ``GIT_TAG``
    The tag, branch, or commit to check out.

  ``SYSTEM``
    Forwarded to FetchContent_Declare; marks the dependency's include
    directories as system headers.

#]=======================================================================]
function(xyz_fetchcontent_declare_cached)
  set(options SYSTEM)
  set(oneValueArgs NAME GIT_REPOSITORY GIT_TAG)
  cmake_parse_arguments(XYZ "${options}" "${oneValueArgs}" "" ${ARGN})
  if(NOT XYZ_NAME)
    message(FATAL_ERROR "NAME parameter must be supplied")
  endif()
  if(NOT XYZ_GIT_REPOSITORY)
    message(FATAL_ERROR "GIT_REPOSITORY parameter must be supplied")
  endif()
  if(NOT XYZ_GIT_TAG)
    message(FATAL_ERROR "GIT_TAG parameter must be supplied")
  endif()

  if(XYZ_CMAKE_FETCHCONTENT_CACHE_DIR)
    string(TOUPPER "${XYZ_NAME}" XYZ_NAME_UPPER)
    set(XYZ_SOURCE_DIR "${XYZ_CMAKE_FETCHCONTENT_CACHE_DIR}/${XYZ_NAME}-src")

    file(MAKE_DIRECTORY "${XYZ_CMAKE_FETCHCONTENT_CACHE_DIR}")
    # Guards the clone below; each build tree's compiled output stays
    # private, so nothing else here touches shared state.
    file(
      LOCK "${XYZ_CMAKE_FETCHCONTENT_CACHE_DIR}/${XYZ_NAME}.lock"
      GUARD PROCESS
      TIMEOUT 60
      RESULT_VARIABLE XYZ_LOCK_RESULT)
    if(NOT XYZ_LOCK_RESULT EQUAL 0)
      message(
        FATAL_ERROR
          "Could not lock ${XYZ_CMAKE_FETCHCONTENT_CACHE_DIR}: ${XYZ_LOCK_RESULT}"
      )
    endif()

    if(NOT EXISTS "${XYZ_SOURCE_DIR}/CMakeLists.txt")
      execute_process(
        COMMAND git clone --depth 1 --branch "${XYZ_GIT_TAG}"
                "${XYZ_GIT_REPOSITORY}" "${XYZ_SOURCE_DIR}"
        RESULT_VARIABLE XYZ_CLONE_RESULT)
      if(NOT XYZ_CLONE_RESULT EQUAL 0)
        message(FATAL_ERROR
                "Failed to clone ${XYZ_GIT_REPOSITORY} into ${XYZ_SOURCE_DIR}")
      endif()
    endif()

    set(FETCHCONTENT_SOURCE_DIR_${XYZ_NAME_UPPER}
        "${XYZ_SOURCE_DIR}"
        PARENT_SCOPE)
  endif()

  if(XYZ_SYSTEM)
    FetchContent_Declare(
      ${XYZ_NAME}
      GIT_REPOSITORY ${XYZ_GIT_REPOSITORY}
      GIT_TAG ${XYZ_GIT_TAG}
      SYSTEM)
  else()
    FetchContent_Declare(
      ${XYZ_NAME}
      GIT_REPOSITORY ${XYZ_GIT_REPOSITORY}
      GIT_TAG ${XYZ_GIT_TAG})
  endif()
endfunction()
