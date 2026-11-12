# Build and test

How FLux applies the [verify skill](../../.agents/skills/verify/SKILL.md).

- Configure the main worktree with `cmake --preset developer`. `tools/worktree-add.sh` configures each other worktree. Build with `cmake --build --preset developer`. The `binaryDir` of the preset in `CMakePresets.json` is the build tree.
- The option `KRR_BUILD_FLUX_HYDRA` of the root `CMakeLists.txt` builds hdFlux. Its description says what it needs.
- `krr_add_test` in `cmake/KRR_AddTest.cmake` names each unit test target, such as `flux.CLI.CLITests`, and places its binary. Build one with `--target`.
- `ctest` in the build tree runs every test. `flux/Tests/CMakeLists.txt` labels the integration tests `integration`.
- To compare two versions, build each in its own tree, and set `OPTIX_CACHE_MAXSIZE=0` to turn off the OptiX disk cache.
