# Runs the CLI and checks that it succeeds and writes OUTPUT or does not, as EXPECT says.
# Usage: cmake -DFLUX=<executable> -DSCENE=<toml> -DOUTPUT=<file> -DEXPECT=<exists|absent>
#              [-DSPP=<count>] -P CheckCLIOutput.cmake
file(REMOVE "${OUTPUT}")
set(arguments "${SCENE}" --backend embree --output "${OUTPUT}")
if(DEFINED SPP)
    list(APPEND arguments --spp ${SPP})
endif()
execute_process(COMMAND "${FLUX}" ${arguments} RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "flux exited with ${result}")
endif()
if(EXPECT STREQUAL "exists" AND NOT EXISTS "${OUTPUT}")
    message(FATAL_ERROR "flux wrote no ${OUTPUT}")
endif()
if(EXPECT STREQUAL "absent" AND EXISTS "${OUTPUT}")
    message(FATAL_ERROR "flux wrote ${OUTPUT}")
endif()
