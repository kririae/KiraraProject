include(KRR_AddTest)

if(KRR_BUILD_TESTS)
  krr_add_test(kira Core AnyhowTests
    SOURCES
      AnyhowTests.cpp
    HARD_DEPENDENCIES
      kira::Core
  )

  krr_add_test(kira Core LoggerTests
    SOURCES
      LoggerTests.cpp
    HARD_DEPENDENCIES
      kira::Core
  )

  krr_add_test(kira Core LoggerThreadSafeTests
    SOURCES
      LoggerThreadSafeTests.cpp
    HARD_DEPENDENCIES
      kira::Core
  )
endif()
