include(KRR_AddTest)

if(KRR_BUILD_TESTS)
  krr_add_test(kira Vecteur VecteurStaticTests
    SOURCES
      VecteurStaticTests.cpp
    HARD_DEPENDENCIES
      kira::Vecteur
  )

  krr_add_test(kira Vecteur VecteurDynamicTests
    SOURCES
      VecteurDynamicTests.cpp
    HARD_DEPENDENCIES
      kira::Vecteur
  )

  krr_add_test(kira Vecteur VecteurEigenPlayground
    SOURCES
      VecteurEigenPlayground.cpp
    HARD_DEPENDENCIES
      kira::Vecteur
  )

  add_executable(VecteurTests VecteurBinaryCheck.cpp)
  target_link_libraries(VecteurTests PRIVATE kira::Vecteur)
endif()

if(KRR_BUILD_COMPTIME_TESTS)
  krr_add_test(kira Vecteur VecteurComptimeTests
    SOURCES
      VecteurComptimeTests.cpp
    HARD_DEPENDENCIES
      kira::Vecteur
      ut
  )
endif()