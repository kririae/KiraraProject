include(KRR_Message)

if(KRR_BUILD_TESTS)
  add_executable(LoggerTests LoggerTests.cpp)
  target_link_libraries(LoggerTests PRIVATE kira::Core)
endif()
