# FLux render tests

Render tests compare FLux images with analytical values or Mitsuba references.

## Run

From the repository root:

```sh
uv sync --frozen --project flux/Tests/Integration
cmake --preset developer
cmake --build --preset developer
ctest --test-dir build/developer -L integration --output-on-failure
```

Install `uv` before configuring CMake. By default, CTest checks the white sphere
and checker against analytical values on both backends. OptiX requires a
supported NVIDIA GPU; add `-E '\.optix$'` to run Embree alone.

Generate the Mitsuba references before enabling image comparisons:

```sh
uv run --frozen --project flux/Tests/Integration python flux/Tests/Integration/reference.py \
    --output-dir build/developer/flux/Tests/Integration --variant cuda_ad_rgb
cmake --preset developer -DKRR_FLUX_TEST_REFERENCES=ON
ctest --test-dir build/developer -L integration --output-on-failure
```

Use `--variant llvm_ad_rgb` to generate references on the CPU. To select a
single test, add a CTest filter such as `-R 'principled_cbox\.optix$'`.

## Check failures

CTest reports failed cases. Open
`build/developer/flux/Tests/Integration/<case>/<backend>/` for `render.log`,
`render.exr`, `diff.exr`, and `metrics.json`. The Mitsuba reference is at
`<case>/reference.exr`.

Comparisons use linear RGB at the rendered exposure. `Cases.toml` sets the
mean absolute error (MAE) and root mean square error (RMSE) limits; `diff.exr`
shows absolute error. Sampling noise contributes to both metrics.

If a reference is missing or stale, review the scene changes and rerun
`reference.py`. Add `--case <id>` to regenerate one reference.

## Add a case

Place a FLux TOML scene and its Mitsuba XML counterpart together in `Fixtures`.
Use the same mesh and image files, and match the camera, materials, lighting,
and render settings. Explain any necessary differences beside the scene.

Add the case, sample count, and error limits to `Cases.toml`, then register its
ID in `Tests/CMakeLists.txt`. Use `expected` for an analytical RGB value or
generate a Mitsuba reference. Check the images on both backends before choosing
error limits. Keep generated images and logs in the build directory.
