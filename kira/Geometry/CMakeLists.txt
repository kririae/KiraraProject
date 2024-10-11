CPMAddPackage(
    NAME libigl
    GITHUB_REPOSITORY libigl/libigl
    VERSION 2.5.0
    OPTIONS "LIBIGL_INSTALL ON"
    PATCHES "${KRR_ROOT_DIR}/cmake/patches/libigl.patch")

krr_add_module(kira Geometry HARD_DEPENDENCIES igl::core)
