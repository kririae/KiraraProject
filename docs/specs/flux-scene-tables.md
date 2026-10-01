# FLux Scene and Light Tables

Status: proposed

Evidence: the current `SceneTableData`, `LightTableData`, `EmbreeLightSampler`,
`OptixLightSampler`, and `OptixContext::Sync`; old FLux `Primitive::HasSurfaceLight`,
`SurfaceLight::DeviceData`, and `EstimateSurfaceLightPowerWeight`; MDL 1.9 and the
MaterialX PBR specification; the UsdPreviewSurface specification; the OpenPBR
reference shader.

## Product boundary

Both runtimes read the same host data: which primitives exist, what they bind,
where they are, and how lights are selected. This specification defines who
produces that data, who owns it, and how a runtime addresses it.

It does not define when a runtime rebuilds a device structure. That belongs to a
later specification on incremental scene updates.

The state this replaces: the scene table and the light table each derive the
dense primitive order, the light table writes an index back into the scene table,
and the two orders are kept equal by comparing counts at run time.

## Vocabulary

**Table data.** The host owner of a set of tables, named `<Table>Data`.
`SceneTableData` owns the scene tables and `LightTableData` owns the light tables.

**View.** The pointer-only structure a runtime reads, named `<Table>`.

**Primitive index, geometry index.** The index `Context` assigns to a primitive or
a mesh from its `ContextIndexMap`. It stays unchanged while the object is
registered. Tables are sized by the index limit.

**Hole.** An index with no visible primitive, or a geometry index no visible
primitive references. A hole keeps an empty entry, and no instance, acceleration
structure, or light slot refers to it. Hiding a primitive makes its index a hole
and moves no other index.

Amendment: earlier text called these the *dense* primitive and geometry indices,
ranked by the scene table over visible primitives in Context ID order. The rest of
this specification still says "dense" for them; read it as the index above. See
[FLux Runtime Data Ownership](flux-runtime-ownership.md#primitive-indices).

**Light identity.** The index that names a light within its type: the point-light
index for a point light, the dense primitive index for an emitting primitive, and
nothing for the environment map, which appears at most once.

**Slot.** A position in the light selection distribution. Slots cover every light
type.

## Ownership

### The test question

Computed data belongs to a table when both runtimes would compute it identically,
and to the runtime when they would not. The question is "would OptiX and Embree
compute this differently?".

| Table | Runtime |
| --- | --- |
| primitives, transforms, mesh references, bsdfs, edfs, point lights, area scales, slots, the power CDF | geometry implementations, texture pools, environment-map power and distribution, acceleration structures |

### Impls that carry addresses

An `Impl` whose fields are plain values can be built once and copied into any
address space. An `Impl` that holds pointers is valid only in the address space
it addresses, so it is built by whoever owns that memory.

`Primitive::Impl`, `BSDF::Impl`, `EDF::Impl`, and `PointLight::Impl` hold plain
values, and the tables produce them for both runtimes. `Geometry::Impl` and
`EnvMapLight::Impl` hold pointers into mesh arrays and distributions, so each
runtime builds its own.

Consequence: the scene table holds `Ref<TriangleMesh const>` and each runtime
derives its geometry implementations from those meshes. There is no factory that
takes a geometry pointer as its parameter.

### Index ownership

`Context` assigns BSDF, EDF, image-texture, geometry, and primitive indices,
because it assigns them to objects it owns. The scene table writes the entry at
each index, and a hole where no visible primitive is. The light table assigns
slots and point-light indices. A runtime assigns its own geometry implementation
indices.

## One pass, one producer

Each table data has one `build`, which is the only producer of its tables and
assigns every index those tables contain while it writes the entry that index
addresses.

An index is a claim about another array's contents. Two producers of one index
can disagree, and the disagreement is invisible in the types, so an index and the
array it addresses are produced together.

The scene table's pass writes each visible primitive at the index `Context`
assigned it, so no table defines an order of its own. Nothing writes into a table after
its `build` returns. The light table derives its data from the scene table, never
from the primitives of the `Context`, so the dependency runs one way.

## The scene table

### Segments

`SceneTableData` has two segments, and a field's segment is decided by what it
holds:

```cpp
struct SceneTableData final : private Noncopyable {
    /// Context objects at each index. Host only; read while building runtime
    /// state, never while rendering.
    struct {
        /// Visible primitives by primitive index, or null at a hole.
        kira::SmallVector<Ref<Primitive const>> primitives;
        /// Meshes by geometry index, or null at a hole.
        kira::SmallVector<Ref<TriangleMesh const>> meshes;
    } objects;

    /// Entry of each primitive by primitive index. A hole is a default entry.
    std::vector<Primitive::Impl> primitives;
    /// Row-major object-to-world transforms by primitive index.
    std::vector<std::array<float, 12>> transforms;
    /// BSDFs indexed by the indices assigned by Context.
    std::vector<BSDF::Impl> bsdfs;
    /// EDFs indexed by the indices assigned by Context.
    std::vector<EDF::Impl> edfs;
};
```

- `objects` holds, for each index space the table assigns, the Context object at
  each index. A consumer that needs host-side state of a primitive or mesh reads
  it here at the same index, with no walk of the `Context` and no order to keep
  equal.
- The outer fields hold address-independent entries read while rendering.
  `objects.primitives[i]` and `primitives[i]` are one primitive at two levels.
- Index spaces assigned by `Context` have no `objects` array, because `Context`
  already maps those indices to objects.
- Geometry has no outer table, because its `Impl` carries addresses. The runtime
  builds it from `objects.meshes`.

`objects` is a member of unnamed struct type. A fully anonymous struct, whose
fields are injected into the enclosing class, is a compiler extension and is not
used.

### The primitive entry is local

Every field of `Primitive::Impl` depends only on the primitive's own bindings:

```cpp
struct Primitive::Impl {
    std::uint32_t geometryIndex{};
    std::uint32_t bsdfIndex{invalidBSDFIndex};
    std::uint32_t edfIndex{invalidEDFIndex};
};
```

The removed `primLightIndex` was the rank of a primitive among emitting
primitives. It depended on every other primitive, so the per-primitive pass could
not write it without global state, an EDF edit on one primitive renumbered every
later emitter's entry, and the light table assigned it by writing into the scene
table. Its only use was indirection into light-table arrays, and the hit already
carries the dense primitive index in `SurfaceInteraction::primitiveIndex`, so the
light table indexes by that directly.

The entry shrinks from 16 to 12 bytes. Interleaved runs on an RTX 4070 Laptop GPU
at 512^2 x 256 spp showed no difference against a padded 16-byte entry (Cbox
159.2 vs 159.6 ms, PrincipledCbox 178.9 vs 178.6 ms, medians of 12), so the entry
carries no padding.

## Emission

### Emission comes from an EDF

A primitive emits when it binds an EDF. There is no area light object, and a BSDF
never emits.

A separate area light would duplicate geometry, transform, and emission that the
primitive already holds, and would need a rule for a primitive that has both. Old
FLux had an `AreaLight` and emissive BSDFs, both of which lowered to one
`SurfaceLight::DeviceData`, with a warning when a primitive carried both.

Material models separate the two: MDL has `bsdf` and `edf`, and MaterialX and
OpenPBR likewise pair a scattering model with an emission model. Where emission
depends on coating parameters, OpenPBR expresses it as a distinct EDF and the
adapter copies the parameters it needs.

`UsdPreviewSurface` places `emissiveColor` on the surface shader, so a Hydra
material adapter lowers it into a BSDF and an EDF on the primitive and owns the
resulting object identity.

### Membership

A visible primitive is a light candidate exactly when it binds an EDF, which is
`primitives[i].hasEDF()`. The light table makes this decision, in one place, and
nothing else in the system asks whether a primitive is a light.

There is no `EDF::isEmissive`. A black or degenerate emitter is a candidate whose
power is zero, and zero power is handled by the distribution rather than by
membership.

### Power

```
pi * surfaceArea * areaScale * EDF::estimateLuminance()
```

`Primitive::estimatePower` computes this. Where it is not computable, such as an
image texture's average or an MDL emission whose intensity cannot be evaluated on
the host, an estimate is used. Old FLux uses `0.5` for both.

Power is nonnegative and may be zero, for zero radiance, zero surface area, or a
zero-scale transform. `buildLightPowerCDF` gives a nonpositive weight zero
probability, and equal probability to every light when no weight is positive. A
zero-power emitter keeps its slot, light sampling never selects it, and a
BSDF-sampled hit on it is weighted one by the balance heuristic. The image is
unbiased. The exception is a table with one slot, where `LightPowerDistribution`
returns probability one at any power. That light adds zero radiance, so the
result is still correct.

### Consequences for edits

Binding or unbinding an EDF changes one primitive's `edfIndex` and requires a
light-table rebuild. Changing radiance, a transform, or a mesh changes powers and
area scales and requires a light-table rebuild. None of these renumbers another
primitive's entry.

## The light table

### Segments

```cpp
struct LightTableData final : private Noncopyable {
    static constexpr std::uint32_t invalidSlot = LightPowerDistribution::invalidSlot;

    /// Per-light data, indexed by light identity.
    struct {
        /// Point lights in Context ID order.
        std::vector<PointLight::Impl> points;
        /// World-area scale of each primitive's transform, by primitive index.
        std::vector<float> primAreaScales;
    } lights;

    /// The selection distribution. A slot is a position in it.
    struct {
        /// Light at each slot. A primitive's handle index is its primitive index.
        std::vector<LightHandle> handles;
        /// Cumulative selection weight at each slot.
        std::vector<float> cdf;
        /// Slot of each primitive by primitive index, or invalidSlot.
        std::vector<std::uint32_t> primSlots;
        /// Slot of the environment map, or invalidSlot.
        std::uint32_t envMapSlot{invalidSlot};
    } slots;

public:
    /// \p envMapPower is empty when no environment map is active.
    void build(SceneTableData const &scene, Context const &context,
               std::optional<float> envMapPower,
               std::uint32_t maxSlots = LightPowerDistribution::maxLightCount);
};
```

- `lights` is what a light is. It is indexed by light identity and does not
  change when the selection structure changes.
- `slots` is how a light is chosen: the slot-indexed handles and CDF, and the
  maps from light identity to slot.

Point lights have no slot map. A point light is a delta distribution that no ray
hits, so no caller asks for its selection probability. The environment map is
reached by a ray miss and appears at most once, so its map is one value.

The light table builds the CDF. Both runtimes previously called
`buildLightPowerCDF` on a host `powers` array they read from the table, which is
the computation the test question assigns to the table. Powers are a local of
`build`, and the table has no host-only field.

`primAreaScales` is indexed by primitive rather than by slot so that replacing the
selection structure leaves it in place. It costs four bytes per primitive index,
the same as the removed `primLightIndex`. Computing the scale from
`transforms[primIndex]` on use would remove it, and is deferred.

### Build

`build` assigns slots in this order:

1. Point lights from the `Context`, in Context ID order.
2. The environment map, when `envMapPower` is present.
3. Primitives in index order, for each `i` with `scene.primitives[i].hasEDF()`.
   Power and area scale come from `scene.objects.primitives[i]`, which is also
   where the non-uniform-scale warning reads the Context ID.

Every other `primSlots[i]` is `invalidSlot`, and `primSlots` and
`primAreaScales` have one entry per primitive index. A hole has `invalidSlot` and
an area scale of zero.

### The cap

`maxSlots`, which defaults to `LightPowerDistribution::maxLightCount`, bounds the
slot count, and the cap is applied while slots are assigned. It is a parameter so
that a test can reach the cap without millions of lights. Because point lights and the environment map
are assigned first, a full table drops emitting primitives:

- An emitting primitive past the cap gets `invalidSlot`. Light sampling cannot
  propose it, its selection probability is zero, and a BSDF-sampled hit on it is
  weighted one. The image is unbiased.
- A point light past the cap is dropped. It cannot be reached any other way, so
  this biases the image, and it only happens with more point lights than the cap.

The table logs one warning when the cap drops any light.

### Sampling and evaluation

The sampler selects a slot and returns its handle with the selection
probability. Each light type's sampling returns a sample whose `pdf` is
conditional on that light, in solid-angle measure, and the caller multiplies it
by the selection probability. `pdfDirectLight` for an emitting primitive returns
`pmf(ctx, {Primitive, i}) * conditionalPdf`, where the selector maps the handle to
a slot through `primSlots[i]`.

Keeping the selection probability out of the per-type code is what lets a
spatially varying selector replace `slots` without touching light sampling.

At a hit, `i` is `SurfaceInteraction::primitiveIndex`. Emission at the hit is
added when `primitives[i].hasEDF()`; the MIS weight comes from `primSlots[i]` and
`primAreaScales[i]`, and is one when the slot is invalid.

### Views

Each runtime assembles its view of the light table in two parts that follow the
segments: a `LightTable` with the per-light data,

```cpp
struct LightTable {
    PointLight::Impl const *points{};
    float const *primAreaScales{};
    EnvMapLight::Impl const *envMap{};
};
```

and its sampler's `Impl` with the selection data: `handles`, `primSlots`,
`envMapSlot`, and a `LightPowerDistribution` over `cdf`. The environment map's
`Impl` and distribution stay with the runtime, as before.

### What the light table no longer does

- Walk the `Context` for primitives.
- Assign or write a light index into the scene table.
- Compare its own primitive count against the scene table.
- Leave the CDF to each runtime.

The count check it replaces is worth recording: the light table walked visible
primitives to assign light indices and threw when its count differed from the
scene table's. A count comparison detects a length difference, not an order
difference, so reordering primitives in the scene table would leave every emitter
pointing at another light's slot with the check passing.

## Views

A runtime assembles its own views from memory it owns, and the table data exposes
fields and not a factory:

```cpp
.table = {
    .geometries = geometryImpls_.data(),
    .primitives = table_.primitives.data(),
    .bsdfs = table_.bsdfs.data(),
    .edfs = table_.edfs.data(),
},
```

There is no `getTable` on either table data. The view a runtime needs depends on
which address spaces it owns, and a factory that produces a view from host
pointers is only correct for the runtime that reads host memory.

A view holds what both runtimes read. An array only one runtime reads is a field
of that runtime's own view, such as `EmbreeContext::Impl::normalTransforms`.

`SceneTableData::objects` has no view. It is read while building runtime state and
never while rendering.

## Verification seam

Host tests, no GPU:

1. A rebuild after hiding a primitive equals a build from scratch.
2. An empty context produces empty tables.
3. Two primitives sharing a mesh share one geometry index, and a mesh used only by
   invisible primitives is a hole.
4. Each visible primitive sits at the index `Context` assigned it,
   `objects.primitives[i]` is the primitive whose entry is `primitives[i]`, and
   hiding a primitive or adding another moves no index.
5. `slots.primSlots` and `lights.primAreaScales` have one entry per primitive
   index. `primSlots[i]` is valid only if `primitives[i].hasEDF()`, and then
   `handles[primSlots[i]] == {Primitive, i}`. Every primitive handle at slot `s`
   has `primSlots[handle.index] == s`.
6. An emitter with zero radiance has a slot and zero selection probability.
7. With more emitting primitives than the cap, every point light and the
   environment map have a slot, and the emitters past the cap have
   `invalidSlot`.

The reference-image tests cover consumption: a light whose power or slot is wrong
changes the image.

## Rejected alternatives

**The light table assigning a light index into the scene table.** It is correct
only while the light table runs before the scene table is uploaded, which no type
expresses. The failure is silent: every emitter gets the invalid index, direct
light sampling still works, and BSDF hits on emitters are weighted one, so
emission is counted twice.

**The scene table assigning a light index while it writes the entry.** It gives
the index one producer but leaves a non-local field in `Primitive::Impl`: an EDF
edit on one primitive renumbers every later emitter's entry, and the scene table
takes on light-sampling data such as area scale and power. Indexing light data by
dense primitive index removes the field instead.

**An emitter table, `PrimitiveLight { primIndex, areaScale }`, indexed by a
prim-light index.** It is a third index space between the dense primitive index
and the slot, with an invariant tying it to both. Its only content is the
primitive index, which the handle already carries, and the area scale, which
`lights.primAreaScales` holds.

**Keeping the two orders equal by comparing counts at run time.** A count
comparison detects a length difference, not an order difference.

**A factory on the table data that takes a geometry pointer.** `Geometry::Impl` is
the only impl that carries pointers, so the factory took exactly that one as a
parameter. The signature hid a general rule as a single exception, and it read as
if geometry were special.

**An `AreaLight` host object.** The primitive is the area light. A separate object
duplicates state and needs a conflict rule.

**Copying scene data into the light entry.** Old FLux stores geometry, emission,
and an area scale in the light entry while also keeping an `ias_instance_id`
pointing back at the primitive. One edit then updates two structures, which is
what its sync has to keep coherent by hand.

**`EDF::isEmissive` deciding membership.** It moves a primitive in and out of the
light table on a radiance edit, and it duplicates what zero power already does
without bias.

**Excluding EDFs whose power cannot be estimated.** A texture's power is unknown
rather than zero, and dropping the light biases the image.

**Dropping point lights at the cap.** A point light cannot be found by BSDF
sampling, so dropping one biases the image, while an emitting primitive past the
cap stays reachable.

**Storing emission in the BSDF.** It needs a conflict rule for a primitive that
has both, and every model that authors the two separately must still be split
somewhere.

## Deferred

- Textured EDF, and where its power estimate comes from.
- Two-sided emission. `ConstantEDF` emits on one side today, and side is a
  property of the EDF.
- Light trees and other spatially varying selection. A leaf is named by light
  identity, and the map from primitive to leaf replaces `primSlots`. OptiX is
  expected to move to a light tree while Embree keeps the power distribution, so
  each runtime's sampler `Impl` keeps its own `sample` and `pmf`.
- A per-type light `Impl` for emitting primitives, with `sampleDirect` and `pdf`
  as members instead of the free functions in `LightSamplingImpl.h`.
- Computing area scale from the transform on use, which removes
  `lights.primAreaScales`.
- Per-object derived data on the object itself, such as a mesh's triangle
  sampling distribution.

## Staging

1. Scene table split: `SceneTableData` and `SceneTable`, and the geometry
   implementations moved to each runtime. Committed in `b8c7ee6`.
2. `SceneTableData::objects`, holding the visible primitives and the meshes.
   Implemented.
3. The light table rebuilt from the scene table with the `lights` and `slots`
   segments, the CDF built by the table, `primLightIndex` and `pointSlots`
   removed, and `pdfDirectLight` indexed by `SurfaceInteraction::primitiveIndex`.
   Implemented; the `Primitive::Impl` size change is measured above.
4. The host tests above, in `SceneTableTests` and `LightTableTests`.
5. Incremental scene updates, in their own specification.
