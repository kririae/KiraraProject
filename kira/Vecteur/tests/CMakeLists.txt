include(KRR_AddTest)

if(KRR_BUILD_TESTS)
  krr_add_test(kira Vecteur VecteurGenericTests
    SOURCES
      VecteurGenericTests.cpp
    HARD_DEPENDENCIES
      kira::Vecteur
  )

  krr_add_test(kira Vecteur VecteurDynamicTests
    SOURCES
      VecteurDynamicTests.cpp
    HARD_DEPENDENCIES
      kira::Vecteur
  )
endif()

if(KRR_BUILD_COMPTIME_TESTS)
  krr_add_test(kira Vecteur VecteurComptimeTests
    SOURCES
      VecteurComptimeTests.cpp
      VecteurTraitsTests.cpp
    HARD_DEPENDENCIES
      kira::Vecteur
      ut
  )
endif()