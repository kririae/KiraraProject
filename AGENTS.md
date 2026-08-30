# FLux Development Rules

## Input handling

- Validate scene input with an exception only when continuing could access invalid memory.
- Propagate failures from required system APIs.
- Bad scene values are not exceptional. Bound them to the supported domain and keep rendering.
- Do not add defensive NaN or infinity checks to rendering code. Let arithmetic propagate unless it can make memory access unsafe.

## Naming

- Prefer short, LLVM-like names. Define domain terms clearly, then use them consistently.
- Do not lengthen a name to repeat context already supplied by its type or scope.
- For example, inside `OptixGeometryPool::build`, prefer `meshes` to
  `optixGeometryPoolMeshes`; the class and function already supply that context.

## Context identity

- Context IDs only increase. They identify objects and serve as lookup keys.
- Backend and device indices come from explicit maps. They have no numeric relation to Context
  IDs. For example, use `context.getBSDFIndex(bsdf->getContextId())`; do not cast a Context ID and
  use it as an array index.
