include_guard(GLOBAL)

if(ENABLE_SANITIZERS)
  set(SANITIZER_FLAGS_ASAN "-fsanitize=address" "-fno-omit-frame-pointer")
  set(SANITIZER_FLAGS_UBSAN "-fsanitize=undefined")
  # The null checks disable -fdelete-null-pointer-checks, so GCC's constexpr
  # evaluator stops folding `&object == nullptr` for std::define_static_string
  # results and rejects protocol.hh's consteval aggregate generation. Exclude
  # them; the exclusion has to follow -fsanitize=undefined to take effect.
  list(APPEND SANITIZER_FLAGS_UBSAN
       "-fno-sanitize=null,nonnull-attribute,returns-nonnull-attribute")
  set(SANITIZER_FLAGS_TSAN "-fsanitize=thread")

  include(CheckCXXCompilerFlag)

  # Check ASAN
  set(CMAKE_REQUIRED_FLAGS "-fsanitize=address -fno-omit-frame-pointer")
  set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=address")
  check_cxx_compiler_flag("-fsanitize=address" COMPILER_SUPPORTS_ASAN)

  # Check UBSAN
  set(CMAKE_REQUIRED_FLAGS "-fsanitize=undefined")
  set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=undefined")
  check_cxx_compiler_flag("-fsanitize=undefined" COMPILER_SUPPORTS_UBSAN)

  # Check TSAN
  set(CMAKE_REQUIRED_FLAGS "-fsanitize=thread")
  set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=thread")
  check_cxx_compiler_flag("-fsanitize=thread" COMPILER_SUPPORTS_TSAN)


  # Reset required flags
  unset(CMAKE_REQUIRED_FLAGS)
  unset(CMAKE_REQUIRED_LINK_OPTIONS)

  if(COMPILER_SUPPORTS_ASAN)
    add_library(asan INTERFACE IMPORTED)
    set_target_properties(
      asan PROPERTIES INTERFACE_COMPILE_OPTIONS "${SANITIZER_FLAGS_ASAN}"
                      INTERFACE_LINK_OPTIONS "${SANITIZER_FLAGS_ASAN}")
  endif(COMPILER_SUPPORTS_ASAN)

  if(COMPILER_SUPPORTS_UBSAN)
    add_library(ubsan INTERFACE IMPORTED)
    set_target_properties(
      ubsan PROPERTIES INTERFACE_COMPILE_OPTIONS "${SANITIZER_FLAGS_UBSAN}"
                       INTERFACE_LINK_OPTIONS "${SANITIZER_FLAGS_UBSAN}")
  endif(COMPILER_SUPPORTS_UBSAN)

  if(COMPILER_SUPPORTS_TSAN)
    add_library(tsan INTERFACE IMPORTED)
    set_target_properties(
      tsan PROPERTIES INTERFACE_COMPILE_OPTIONS "${SANITIZER_FLAGS_TSAN}"
                      INTERFACE_LINK_OPTIONS "${SANITIZER_FLAGS_TSAN}")
  endif(COMPILER_SUPPORTS_TSAN)
endif(ENABLE_SANITIZERS)
