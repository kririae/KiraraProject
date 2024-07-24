include(KRR_Message)
find_program(MOLD_LINKER mold)

if(MOLD_LINKER)
  krr_message(INFO "mold is found at ${MOLD_LINKER}, using mold as the linker")

  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_link_options("-fuse-ld=mold")
  else()
    krr_message(ERROR "mold linker is not supported for the current compiler: ${CMAKE_CXX_COMPILER_ID}")
  endif()
else()
  krr_message(ERROR "mold linker not found, consider setting `KRR_USE_MOLD=OFF`")
endif()
