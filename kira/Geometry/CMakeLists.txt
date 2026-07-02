find_package(libigl CONFIG REQUIRED)

krr_add_module(kira Geometry HARD_DEPENDENCIES igl::igl_core)
