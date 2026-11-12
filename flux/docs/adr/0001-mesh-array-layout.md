# Mesh arrays carry no layout that a backend requires

## Context

FLux must handle meshes of tens of GiB, so the peak host memory of a mesh array matters. The target is about two copies of each array at the worst moment of a load or an edit: one that the host application holds, such as USD, and one that FLux holds. An edit holds up to three until `Context::collectGarbage`, which this decision does not change.

For the points, a vertex is 12 bytes. A closed triangle mesh has about half as many vertices as triangles, so one copy of the points is about 6 bytes per triangle. At a billion triangles, each copy is about 5.6 GiB.

Embree reads 16 bytes from the last `RTC_FORMAT_FLOAT3` vertex of a buffer. OptiX reads no more than the array. A producer that shares memory it does not own, such as a `VtArray`, cannot give it spare capacity, so it would have to copy it for every backend.

## Decision

Core mesh arrays carry no layout requirement of a backend. A backend that needs more adapts at its own boundary: it shares a buffer that fits, and copies one that does not.

- `EmbreeContext::sync` shares a vertex buffer with spare capacity, `capacity() > size()`. It copies any other into a buffer from `rtcSetNewGeometryBuffer`, which Embree pads. Index buffers stay shared.
- A producer that allocates its own buffer gives the vertices spare capacity, since that costs nothing.
- A producer that shares memory it does not own shares it as it is.
- No FLux code reads beyond `size()` of a buffer, except Embree's 16-byte read of the last vertex. That read stays inside the `capacity()` of a shared buffer, or inside the buffer that Embree padded. So an array that USD maps straight from a file never faults.

## Consequences

The table counts copies of the points in host memory. A copy is one array of the points. `U` is the array of the host application, `F` is the FLux buffer, and `E` is a copy that Embree holds. Device memory is not counted.

The window is the time between an edit that replaces the points and the collection that frees the old mesh.

| Producer | Backend | Load peak | Steady state | Window |
|---|---|---|---|---|
| Loader | OptiX | 2 (reader's copy, `F`) | 1 (`F`) | none, a loaded mesh is not edited |
| Loader | Embree | 2 | 1 (`F`, shared) | none |
| Loader | Both | 2 | 1 | none |
| hdFlux | OptiX | 2 | 2 (`U`, `F`) | 3 (`U`, old `F`, new `F`) |
| hdFlux | Embree | 2 | 2 (`U`, `F` shared) | 3 |
| hdFlux | Both | 2 | 2 | 3 |
| Producer that shares `U` as `F` | OptiX | 1 | 1 | 2 (old `F`, new `F`) |
| Producer that shares `U` as `F` | Embree | 2 | 2 (`F`, `E`) | 3 (old `F`, new `F`, old `E`) |
| Producer that shares `U` as `F` | Both | 2 | 2 | 3 |

The counts come from these facts:

- A loader allocates the vertices with one spare element, so Embree shares them. `loadObj` holds the parsed positions and `F` at once, 2 copies, until it releases the positions. `loadPly` holds the vertex element that miniply read into the buffer of its reader, and `F`, at least 2 copies, until the reader is destroyed when `loadPly` returns. That buffer holds every property of the vertex rows, so it is larger than one copy of the points when the rows hold normals or texture coordinates.
- hdFlux copies the points of USD into a buffer with one spare element, in `copyPoints`. `HdFluxMesh::points_` and the `TriangleMesh` share that buffer, so they count once.
- An edit of the points in hdFlux builds a new `F`, and `setGeometry` drops the old mesh. The old mesh stays in the `Context` until `Context::collectGarbage`, which `HdFluxRenderDelegate::CommitResources` calls before the sync.
- OptiX keeps no host handle to the points once its upload finishes. `OptixGeometryPool::add` hands the buffer to the deletion queue, which releases it at the first `deleteFinished` after the upload completes, in `OptixHandler::sync` or `render`. The old `F` therefore leaves the window at the later of that call and the collection.
- Embree keeps the `TriangleMesh::Data` in each `GeometryEntry` until `EmbreeContext::reset`, which `sync` calls first. The old `F` leaves the window at that call, after the collection. `reset` also releases the old `E` before `sync` allocates a new one, so the new `E` never lives beside the old one.
- hdFlux drives an OptiX handler only. Its Embree and Both rows follow from `EmbreeContext` alone.
- A `Context` that both handlers sync holds one `F`. Only Embree adds a host copy, `E`.

The cost of the decision is one copy of the vertices for each mesh that a producer shares without spare capacity, and only with Embree.

## Alternatives

- **Spare capacity on every vertex buffer.** This puts the need of one backend into the contract of all producers. A producer that shares memory it does not own must copy it, for OptiX too, so a producer that shares a `VtArray` holds 2 copies under every backend, where the table gives 1 under OptiX. In exchange, every copy is explicit, at the producer, and a backend never pays a cost that the producer cannot see. It loses because the copy that this decision adds happens only under Embree, and a producer that allocates its own buffer avoids it by giving spare capacity.
- **Embree always copies.** The contract is then free of layout, and Embree needs no capacity check. But a loader, which shares its buffer, would hold 2 copies under Embree in the steady state, where the table gives 1, and each sync would also copy every padded mesh, since `sync` already rebuilds every mesh.
