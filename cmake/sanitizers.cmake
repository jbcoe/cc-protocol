include_guard(GLOBAL)

if(ENABLE_SANITIZERS)
  set(SANITIZER_FLAGS_ASAN "-fsanitize=address" "-fno-omit-frame-pointer")
  set(SANITIZER_FLAGS_UBSAN "-fsanitize=undefined")
  set(SANITIZER_FLAGS_TSAN "-fsanitize=thread")

  include(CheckCXXCompilerFlag)

  if(ENABLE_ASAN)
    # Check ASAN
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=address")
    check_cxx_compiler_flag("-fsanitize=address" COMPILER_SUPPORTS_ASAN)
    if(NOT COMPILER_SUPPORTS_ASAN)
      message(
        FATAL_ERROR
          "AddressSanitizer is requested (ENABLE_ASAN=ON) but not supported by the compiler (${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION})."
      )
    endif()
    add_library(asan INTERFACE IMPORTED)
    set_target_properties(
      asan PROPERTIES INTERFACE_COMPILE_OPTIONS "${SANITIZER_FLAGS_ASAN}"
                      INTERFACE_LINK_OPTIONS "${SANITIZER_FLAGS_ASAN}")
  endif()

  if(ENABLE_UBSAN)
    # Check UBSAN
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=undefined")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=undefined")
    check_cxx_compiler_flag("-fsanitize=undefined" COMPILER_SUPPORTS_UBSAN)
    if(NOT COMPILER_SUPPORTS_UBSAN)
      message(
        FATAL_ERROR
          "UndefinedBehaviorSanitizer is requested (ENABLE_UBSAN=ON) but not supported by the compiler (${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION})."
      )
    endif()
    add_library(ubsan INTERFACE IMPORTED)
    set_target_properties(
      ubsan PROPERTIES INTERFACE_COMPILE_OPTIONS "${SANITIZER_FLAGS_UBSAN}"
                       INTERFACE_LINK_OPTIONS "${SANITIZER_FLAGS_UBSAN}")
  endif()

  if(ENABLE_TSAN)
    # Check TSAN
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=thread")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=thread")
    check_cxx_compiler_flag("-fsanitize=thread" COMPILER_SUPPORTS_TSAN)
    if(NOT COMPILER_SUPPORTS_TSAN)
      message(
        FATAL_ERROR
          "ThreadSanitizer is requested (ENABLE_TSAN=ON) but not supported by the compiler (${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION})."
      )
    endif()
    add_library(tsan INTERFACE IMPORTED)
    set_target_properties(
      tsan PROPERTIES INTERFACE_COMPILE_OPTIONS "${SANITIZER_FLAGS_TSAN}"
                      INTERFACE_LINK_OPTIONS "${SANITIZER_FLAGS_TSAN}")
  endif()

  # Reset required flags
  unset(CMAKE_REQUIRED_FLAGS)
  unset(CMAKE_REQUIRED_LINK_OPTIONS)
endif(ENABLE_SANITIZERS)
