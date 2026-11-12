set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)

# One process runs one oneTBB runtime. A host application such as Houdini loads its own, which
# the core then shares through the dynamic library's soname.
if(PORT STREQUAL "tbb")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()

# vcpkg's tiff installs no Tiff CMake package, so a Tiff package that CMake finds belongs to the
# system. Without one, FindTIFF takes vcpkg's library.
list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS -DCMAKE_DISABLE_FIND_PACKAGE_Tiff=ON)

# minizip-ng otherwise prefers a zlib-ng it finds on the system over vcpkg's zlib.
if(PORT STREQUAL "minizip-ng")
    list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS -DMZ_ZLIB_FLAVOR=zlib)
endif()
