set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

# One process runs one oneTBB runtime. A host application such as Houdini loads its own, which
# the core then shares through the dynamic library's soname.
if(PORT STREQUAL "tbb")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
