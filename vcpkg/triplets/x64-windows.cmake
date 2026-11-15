set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

# One process runs one oneTBB and one MaterialX. A host application such as Houdini loads its own,
# which the core then shares through the name of the dynamic library.
if(PORT STREQUAL "materialx" OR PORT STREQUAL "tbb")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
