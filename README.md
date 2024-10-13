# KiraraProject

## Requirements

- GCC 11.4 or higher
- Clang 16.0 or higher (18.0 or higher recommended)
- MSVC 17.8 (19.38) or higher

## Subprojects

- **KIRA**: A minimal, high-performance shared graphics infrastructure.
- **Kirara Dance** (Work in Progress): An effect-oriented animation and simulation engine.

## Building and Usage

KIRA is designed to be built out-of-the-box, while other subprojects may require additional setup. Some special
dependencies need to be configured manually.

### Building KIRA

KIRA uses CMake for building. We recommend using `CMakePresets.json` for a streamlined build process:

For users:

```bash
cmake --preset default
cmake --build --preset release
```

For developers:

```bash
cmake --preset developer
cmake --build --preset developer
```

While the conventional two-step CMake process is supported, it's not recommended.

### Using KIRA in Other Projects

KIRA can be integrated into other projects as a dependency in two ways:

1. Using FetchContent:
   ```cmake
   set(KRR_BUILD_TESTS OFF)
   FetchContent_Declare(
     kira
     GIT_REPOSITORY https://github.com/kririae/KiraraProject.git
     GIT_TAG main)
   FetchContent_MakeAvailable(kira)
   ```

2. Using find_package (requires CMAKE_PREFIX_PATH to be set correctly):
   ```cmake
   find_package(kira CONFIG REQUIRED)
   ```

For concrete usage examples, please refer to the `examples/` directory in the repository.