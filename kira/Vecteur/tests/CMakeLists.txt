include(KRR_AddTest)

if(KRR_BUILD_TESTS)
  krr_add_test(kira Vecteur StaticTests
    SOURCES
      StaticTests.cpp
    HARD_DEPENDENCIES
      kira::Vecteur
  )

  krr_add_test(kira Vecteur DynamicTests
    SOURCES
      DynamicTests.cpp
    HARD_DEPENDENCIES
      kira::Vecteur
  )

  krr_add_test(kira Vecteur EigenPlayground
    SOURCES
      EigenPlayground.cpp
    HARD_DEPENDENCIES
      kira::Vecteur
  )

  add_executable(VecteurTests BinaryCheck.cpp)
  target_link_libraries(VecteurTests PRIVATE kira::Vecteur)
endif()

if(KRR_BUILD_COMPTIME_TESTS)
  krr_add_test(kira Vecteur ComptimeTests
    SOURCES
      ComptimeTests.cpp
    HARD_DEPENDENCIES
      kira::Vecteur
      ut
  )
endif()