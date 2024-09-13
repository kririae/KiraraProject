include(CheckCXXCompilerFlag)
include(KRR_Message)

krr_message(INFO "Native build is enabled. This optimizes the code for the
   current CPU architecture, potentially improving performance but reducing
   portability. The resulting binary may not run on different CPU models")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  check_cxx_compiler_flag("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)

  if(COMPILER_SUPPORTS_MARCH_NATIVE)
    add_compile_options(-march=native)
  endif()

  check_cxx_compiler_flag("-mtune=native" COMPILER_SUPPORTS_MTUNE_NATIVE)

  if(COMPILER_SUPPORTS_MTUNE_NATIVE)
    add_compile_options(-mtune=native)
  endif()
elseif(MSVC)
  # For MSVC, use the appropriate flags for enabling AVX2 or the highest available instruction set
  # Note: MSVC doesn't have an exact equivalent to -march=native
  check_cxx_compiler_flag("/arch:AVX2" COMPILER_SUPPORTS_AVX2)

  if(COMPILER_SUPPORTS_AVX2)
    add_compile_options(/arch:AVX2)
  endif()
endif()
