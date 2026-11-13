include(KRR_Message)
find_program(MOLD_LINKER mold REQUIRED)
set(CMAKE_LINKER_TYPE MOLD)
krr_message(INFO "Using mold as the linker: ${MOLD_LINKER}")
