include(KRR_AddModule)

# ----------------------------------------------------------
# Setup module-specific dependencies
# ----------------------------------------------------------
CPMAddPackage(
  NAME highway
  URL https://github.com/google/highway/archive/refs/tags/1.2.0.tar.gz
  OPTIONS
    "HWY_FORCE_STATIC_LIBS ON"
    "HWY_CMAKE_RVV OFF"
    "HWY_ENABLE_CONTRIB OFF"
    "HWY_ENABLE_EXAMPLES OFF"
    "HWY_ENABLE_INSTALL ON"
    "HWY_ENABLE_TESTS OFF"
)

CPMAddPackage(
  NAME Eigen3
  URL https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.zip
  OPTIONS
    "BUILD_TESTING OFF"
)

# ----------------------------------------------------------
# Do add module
# ----------------------------------------------------------
krr_add_module(kira Vecteur
  HEADERS
    kira/detail/VecteurLazyArith.h
    kira/detail/VecteurReductionArith.h
    kira/Vecteur.h
    kira/VecteurBase.h
    kira/VecteurFormat.h
    kira/VecteurGeneric.h
    kira/VecteurHighway.h
    kira/VecteurLazy.h
    kira/VecteurOptimizer.h
    kira/VecteurRouter.h
    kira/VecteurStorage.h
    kira/VecteurTraits.h
  HARD_DEPENDENCIES
    eigen
    hwy
    kira::Core
  CMAKE_SUBDIRS
    tests
)

if(KRR_ENABLE_CLANG_TIDY)
  krr_enable_clang_tidy(kira::Vecteur)
endif()
