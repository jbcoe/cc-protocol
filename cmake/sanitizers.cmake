include_guard(GLOBAL)

include(CheckCXXCompilerFlag)

set(SANITIZER_FLAGS_ASAN "-fsanitize=address" "-fno-omit-frame-pointer")
set(SANITIZER_FLAGS_UBSAN "-fsanitize=undefined")
set(SANITIZER_FLAGS_TSAN "-fsanitize=thread")
set(SANITIZER_LABEL_ASAN "AddressSanitizer")
set(SANITIZER_LABEL_UBSAN "UndefinedBehaviorSanitizer")
set(SANITIZER_LABEL_TSAN "ThreadSanitizer")

foreach(sanitizer ASAN UBSAN TSAN)
  if(ENABLE_${sanitizer})
    list(JOIN SANITIZER_FLAGS_${sanitizer} " " sanitizer_probe_flags)
    set(CMAKE_REQUIRED_FLAGS "${sanitizer_probe_flags}")
    set(CMAKE_REQUIRED_LINK_OPTIONS "${SANITIZER_FLAGS_${sanitizer}}")
    check_cxx_compiler_flag("${sanitizer_probe_flags}" COMPILER_SUPPORTS_${sanitizer})
    unset(CMAKE_REQUIRED_FLAGS)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
    if(NOT COMPILER_SUPPORTS_${sanitizer})
      message(
        FATAL_ERROR
          "${SANITIZER_LABEL_${sanitizer}} is requested (ENABLE_${sanitizer}=ON) but not supported by the compiler (${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION})."
      )
    endif()
    string(TOLOWER "${sanitizer}" sanitizer_target)
    # GLOBAL: xyz_add_test() calls include(sanitizers) from whichever
    # directory adds the first test target, and a non-GLOBAL IMPORTED
    # target is only visible in that directory and its children.
    add_library(${sanitizer_target} INTERFACE IMPORTED GLOBAL)
    set_target_properties(
      ${sanitizer_target}
      PROPERTIES INTERFACE_COMPILE_OPTIONS "${SANITIZER_FLAGS_${sanitizer}}"
                 INTERFACE_LINK_OPTIONS "${SANITIZER_FLAGS_${sanitizer}}")
  endif()
endforeach()
