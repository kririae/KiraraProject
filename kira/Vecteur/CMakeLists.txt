include(KRR_AddModule)

# ----------------------------------------------------------
# Setup module-specific dependencies
# ----------------------------------------------------------
find_package(hwy CONFIG REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)

# ----------------------------------------------------------
# Do add module
# ----------------------------------------------------------
krr_add_module(
    kira Vecteur
    HEADERS kira/Vecteur/detail/Lazy.h
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
    HARD_DEPENDENCIES Eigen3::Eigen hwy::hwy kira::Core
    CMAKE_SUBDIRS tests)

if(KRR_ENABLE_CLANG_TIDY)
    krr_enable_clang_tidy(kira::Vecteur)
endif()
