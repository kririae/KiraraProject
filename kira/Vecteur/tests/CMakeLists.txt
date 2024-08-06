include(KRR_AddTest)

if(KRR_BUILD_TESTS)
  krr_add_test(kira Vecteur VecteurComptimeTests
    SOURCES
      VecteurComptimeTests.cpp
    HARD_DEPENDENCIES
      kira::Vecteur
      ut
  )
endif()
