# Build and test

How FLux applies the [verify skill](../../.agents/skills/verify/SKILL.md).

- Clone with `git clone --recurse-submodules`. Install CMake, Ninja, and a CUDA Toolkit that supports the host compiler.
- On Windows, configure from an x64 Visual Studio developer shell. To use Clang for C and C++, set `CC` and `CXX` to `clang-cl` before the first configure. NVCC still uses MSVC for CUDA host code.
- Configure the main worktree with `cmake --preset flux`. Configure other worktrees with `tools/worktree-add.sh --preset flux <path>`. Build with `cmake --build --preset flux`.
- The option `KRR_BUILD_FLUX_HYDRA` of the root `CMakeLists.txt` builds hdFLux. `flux/Hydra/CMakeLists.txt` says what it needs.
- `krr_add_test` in `cmake/KRR_AddTest.cmake` names each unit test target, such as `flux.CLI.CLITests`, and places its binary. Build one with `--target`.
- `ctest` in the build tree runs every test. `flux/Tests/CMakeLists.txt` labels the integration tests `integration`.
- To compare two versions, build each in its own tree, and set `OPTIX_CACHE_MAXSIZE=0` to turn off the OptiX disk cache.

## Packages

Each package is an install component, which `krr_add_package` in `cmake/KRR_Packaging.cmake` declares. Install a component to a staging directory, from an x64 Visual Studio developer shell on Windows, or generate the archive of every package:

```sh
cmake --install build/flux --component flux --prefix <staging-directory>
cpack --config build/flux/CPackConfig.cmake
```

Validate a package from its extracted directory, without build or CUDA Toolkit directories on `PATH`: run `bin/flux` on a scene, and render hdFLux with the matching Houdini `husk`. Houdini loads hdFLux once `HOUDINI_PACKAGE_DIR` names the `packages` directory of its package.
