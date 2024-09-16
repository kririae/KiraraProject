# KiraraProject

## Requirements

- GCC 11.4 or higher
- Clang 16.0 or higher (18.0 or higher recommended)
- MSVC 17.8 (19.38) or higher

## Subprojects

- **KIRA**: A minimum and high-performance shared graphics infrastructure.
- **FLux-v2**(WIP): A KIRA's port of our previous offline rendering engine.
- **Kirara Dance**(WIP): An effect-oriented animation and simulation engine.

## Build and Use

KIRA is designed to be built out-of-box, while others are only built with
best-effort guarantee. That is to say, some special dependencies are to be setup
on your own.

KIRA is built by CMake. User can build it with `CMakePresets.json` aware

```bash
cmake --preset default
cmake --build --preset release
```

Developers might want to use

```bash
cmake --preset developer
cmake --build --preset developer
```

One can also use the conventional 2-step CMake process, but it is not
recommended.

Specifically for KIRA, which can be used as a dependency in other projects,
supports 2 modes:

```cmake
set(KRR_BUILD_TESTS OFF)
FetchContent_Declare(
  kira
  GIT_REPOSITORY https://github.com/kririae/KiraraProject.git
  GIT_TAG main)
FetchContent_MakeAvailable(kira)
```

and

```cmake
# Require CMAKE_PREFX_PATH to be set properly
find_package(kira CONFIG REQUIRED)
```

checkout `examples/` directory for concrete usage.
