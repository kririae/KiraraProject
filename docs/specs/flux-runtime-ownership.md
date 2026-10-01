# FLux Runtime Data Ownership

Status: accepted

Evidence: the current `SceneTableData`, `EmbreeContext::sync`, `OptixGeometryPool`,
`EmbreeImageTexturePool`, `ImageAsset`, and `TriangleMesh`; old FLux
`Context::CollectGarbage`.

## Product boundary

This specification defines what a runtime may hold between two syncs, and how
mesh arrays are shared between a `TriangleMesh` and a runtime without a copy.

It does not define when a runtime rebuilds, or how removed objects are
reclaimed. Those belong to [FLux Incremental Scene Updates](flux-scene-updates.md).
It changes one definition in [FLux Scene and Light Tables](flux-scene-tables.md):
where a primitive's index comes from.

## Vocabulary

**Context object.** An object with a Context ID, owned by a `Context`: a
primitive, a mesh, a BSDF.

**Data resource.** An immutable, shared object without a Context ID:
`ImageAsset`, and the `Shared<HostBuffer<T>>` defined here. A data resource has
no setters and no dirty bits.

**Sync.** The phase in which a runtime reads the `Context` and updates its own
state. No edit overlaps it.

## The rule

A runtime reads context objects only during sync. Between syncs it holds only:

- values it computed, such as `Impl` tables and normal transforms;
- device memory;
- references to data resources.

It never holds a `Ref` to a context object, or a pointer into one, after sync
returns.

The rule is checkable from types: no member of a runtime that outlives sync has
the type `Ref<T>` for a context object `T`.

### Why

- **Reclamation.** A removed primitive is reclaimed explicitly, but the BSDF,
  EDF, and mesh it binds are reclaimed when nothing else references them. A
  runtime that keeps a `Ref` to the primitive keeps its whole subgraph alive.
  Whether an object is reclaimed then depends on which runtimes exist and whether
  each has synced since.
- **Concurrent edits.** Between syncs, other threads call setters. A runtime
  that holds a context object can read it while it changes.

### Current violations

- `SceneTableData::objects` holds `Ref<Primitive const>` and
  `Ref<TriangleMesh const>`, and `SceneTableData` lives in each runtime.
- Embree shares the mesh arrays with `rtcSetSharedGeometryBuffer`, so its scenes
  point into `TriangleMesh` storage after sync. Today this is kept valid only by
  the `Ref<TriangleMesh const>` above.

## Host buffers

The image path already follows the rule. `ImageTexture` is a context object that
holds a `Ref<ImageAsset const>`, and `EmbreeImageTexturePool` keeps that asset
alive after sync. Meshes follow the same layering:

| Layer | Images | Meshes |
| --- | --- | --- |
| Data resource | `ImageAsset` | `Shared<HostBuffer<T>>` |
| Context object | `ImageTexture` | `TriangleMesh` |
| Runtime entry | `EmbreeImageTexturePool::Entry` | Embree's per-mesh entry |

Owning memory and sharing it are two layers.

### HostBuffer owns

`HostBuffer<T>` is the host counterpart of `DeviceBuffer<T>`. Both own one
contiguous array, are move-only, are writable, and have the same interface:

```cpp
/// \brief Owns a contiguous array in host memory.
template <typename T> class HostBuffer final : private Noncopyable {
public:
    HostBuffer() noexcept = default;

    /// \brief Takes \p container, which owns its elements alone.
    template <typename Container> explicit HostBuffer(Container &&container);

    /// \brief Replaces the allocation without preserving its contents.
    void resize(std::size_t count, std::size_t capacity);
    void resize(std::size_t count) { resize(count, count); }
    void clear() noexcept;

    [[nodiscard]] T *data() noexcept;
    [[nodiscard]] T const *data() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    /// Number of elements readable from data(); at least size().
    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::span<T> span() noexcept;
    [[nodiscard]] std::span<T const> span() const noexcept;
};
```

- A producer writes into a `HostBuffer` directly. A loader or normal generation
  needs no intermediate vector.
- The container constructor follows RAII: the buffer owns the container, so
  `data`, `size`, and `capacity` come from it. Moving a `kira::SmallVector` in
  copies no element.
- `DeviceBuffer` gains `capacity()`, equal to `size()`, and a `copyFromHost`
  overload that takes a `HostBuffer`. Copies between the two are explicit, as
  between thrust's host and device vectors.

### Shared shares

```cpp
template <typename T> using Shared = std::shared_ptr<T const>;
```

A producer that shares its result builds it with
`std::make_shared<HostBuffer<T>>()`, fills it, and moves the pointer out. The
move converts `std::shared_ptr<HostBuffer<T>>` to `Shared<HostBuffer<T>>`
without an allocation or a copy, and afterwards nothing can write to it. There is
no separate publish step. `Ref` stays for objects
with an identity; `Shared` is for values and containers without one, and it
works for `DeviceBuffer` too.

### One buffer per array

A buffer holds one array, not a whole mesh. A Hydra edit usually changes the
points and keeps the topology, so a new `TriangleMesh` shares the triangle,
texture-coordinate, and index buffers of the old one and replaces only the
points and the normals generated from them.

### No implicit copy

A shared buffer never changes. To change an array, a caller fills a new
buffer and shares it. There is no copy-on-write: an automatic copy is an
implicit cost, and a builder almost always writes a new array anyway.

### Capacity

Spare capacity replaces the padding element. Embree reads 16 bytes from the last
`RTC_FORMAT_FLOAT3` vertex, so it needs `capacity() > size()` for a vertex
buffer. Loaders allocate one spare element with `resize(count, count + 1)`.

Spare capacity is a contract of every vertex buffer, not a request of one
runtime. A producer that cannot meet it, such as a Hydra adapter sharing a
`VtArray`, copies into a buffer that does. A runtime never copies to meet it:
that would be an implicit cost. Embree throws when a vertex buffer lacks spare
capacity, because reading past it would access invalid memory.

`resize` discards the contents, as `DeviceBuffer::resize` does. There is no
`reserve`: capacity is chosen with the allocation, so no resize keeps old
elements by accident.

### Memory FLux does not own alone

A `VtArray` shares its storage with USD. A writable `HostBuffer` over it could
change USD's data, and asking `VtArray` for writable data would copy it. So such
memory enters only the shared layer, through a factory that returns
`Shared<HostBuffer<T>>`. The buffer keeps the `VtArray` alive behind a
type-erased holder, and its capacity is the array's `capacity()`. The factory is
a template, instantiated in the Hydra layer, so USD types stay out of
`flux/Scene`.

The split between the constructor and the factory is exclusive ownership, not
the kind of container.

## TriangleMesh

`TriangleMesh::Data` holds one `Shared<HostBuffer<T>>` per array, and
`TriangleMesh` holds a `Data`. Building a mesh copies handles, not elements.

- A mesh is never empty. Construction throws when the vertices or the triangles
  are absent, so no consumer handles zero elements. A producer that has an empty
  mesh, such as a Hydra adapter mid-edit, creates no `TriangleMesh`.
- A handle is null exactly when its array is absent. The constructor turns an
  empty buffer into a null handle, as `DeviceBuffer` keeps a null pointer
  exactly when it is empty. There is one form of "absent", and it needs no
  allocation.
- The getters still return spans. A null handle gives an empty span, so a reader
  never sees a handle.
- Generated normals are a new buffer.
- The padding element goes away. `getVertices` returns the whole buffer.
- `checkIndices` reads the buffers. Its contract does not change.

## Runtimes

- **Embree.** One `GeometryEntry` per geometry merges the state Embree already
  keeps per geometry: the `TriangleMesh::Data` it built from, the triangle area
  CDF and PDF, and the child `RTCScene`. It replaces `meshScenes_` and
  `triangleData_`. It holds the whole `Data`, because `Geometry::Impl` points
  into the normal and texture-coordinate buffers too. `geometryImpls_` stays a
  separate array, because `SceneTable::geometries` needs contiguous `Impl`
  values. A vertex buffer without spare capacity is an error; see
  [Capacity](#capacity).
- **OptiX.** It copies to the device during sync and keeps no host handle.

A reclaimed `TriangleMesh` leaves Embree's buffers valid until Embree rebuilds.

## Primitive indices

`SceneTableData::objects.primitives` exists so that the light table can find
the primitive at a dense index. That index is ranked by `SceneTableData`, so a
second consumer needs the table's own map.

Instead, `Context` assigns a primitive's index when it absorbs the primitive,
from a `ContextIndexMap`, as it does for BSDFs and EDFs:

- An index stays unchanged while its primitive is registered.
- A released index can be reused.
- Visibility is a bit of the entry, not membership in the table. Hiding a
  primitive changes no index.
- Tables are sized by `getPrimitiveIndexLimit()`. An unused index is an entry
  with no instance and no light slot.
- The light table walks the Context's primitives during sync and asks
  `getPrimitiveIndex(id)`. There is one source of the index, so there is nothing
  to keep in step.

This replaces "visible primitives in Context ID order" in
[FLux Scene and Light Tables](flux-scene-tables.md). It also makes incremental
updates possible: adding or hiding a primitive no longer moves every later
index.

Geometry indices follow the same rule. `Context` assigns a mesh's index from a
`ContextIndexMap` when it absorbs the mesh, and `SceneTableData` no longer ranks
meshes in each build. Adding a mesh moves no other mesh's index.

`SceneTableData` keeps no context object after `build` returns. The meshes it
visits are a local of `build`, and each runtime takes the buffers it needs.

## Rejected alternatives

**A runtime copies every mesh.** It meets the rule and doubles host memory for
Embree, against the goal of near-zero-copy meshes.

**Deferred reclamation.** The `Context` frees an object only after every runtime
has synced past its removal. This needs runtime registration, and a runtime that
skips frames holds memory without bound.

**One shared, read-only buffer type.** A `Buffer<T>` that is shared from the
start has no form matching `DeviceBuffer`, and no writable stage for a producer.

**Intrusive counting, `Ref<HostBuffer<T>>`.** An atomic count cannot move, so a
`HostBuffer` could never be a value member as `DeviceBuffer` is.

**A slack query.** "Bytes readable after the last element" is
`capacity() - size()`. Size and capacity compose, for example in a slice.

**One resource per mesh.** It mirrors `ImageAsset` exactly, but a points edit
would copy the topology.

**Copy-on-write.** See [No implicit copy](#no-implicit-copy).

**A runtime that copies a buffer without spare capacity.** The copy is an
implicit cost, paid on every sync of a mesh from USD. See [Capacity](#capacity).

**A host buffer as a context object.** It would get an ID, dirty bits, and a place in
reclamation. Nothing names a buffer, and it never changes.

## Verification seam

Host tests, no GPU:

1. A `HostBuffer` built from a vector keeps its data pointer, and converting its
   `std::shared_ptr` to `Shared` keeps it too.
2. A mesh built from another mesh's triangle buffer shares its pointer.
3. Removing a primitive and collecting reclaims its BSDF and mesh, while a
   runtime that has not synced since still holds the mesh's buffers.
4. Hiding a primitive and adding another changes no existing primitive index.

Render tests: Embree renders the same image before and after the `TriangleMesh`
it built from is reclaimed.

## Deferred

- A pool that shares buffers by file path, like `ImageAssetPool`.
- Moving `ImageAsset` pixel storage onto `HostBuffer`.

## Staging

1. `HostBuffer<T>` and `Shared`, `TriangleMesh` arrays as shared buffers, the
   padding element removed, and Embree's `GeometryEntry`.
2. Primitive and geometry indices from `Context`, visibility as an entry bit, the light table
   walking the `Context`, and `SceneTableData::objects` removed. The scene tables
   specification is amended in the same change.
3. Removal and reclamation, per [FLux Incremental Scene Updates](flux-scene-updates.md).
