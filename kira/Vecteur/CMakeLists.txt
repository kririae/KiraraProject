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
    kira/Vecteur/detail/Lazy.h
    kira/Vecteur/detail/ReductionMixin.h
    kira/Vecteur/Base.h
    kira/Vecteur/Format.h
    kira/Vecteur/Generic.h
    kira/Vecteur/Highway.h
    kira/Vecteur/Lazy.h
    kira/Vecteur/Optimizer.h
    kira/Vecteur/Router.h
    kira/Vecteur/Storage.h
    kira/Vecteur/Traits.h
    kira/Vecteur.h
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
