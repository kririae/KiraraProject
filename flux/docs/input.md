# Input

How FLux takes its input: the names that scene content gives, and how it applies the [errors skill](../../.agents/skills/errors/SKILL.md).

## Asset paths

FLux uses the vocabulary of USD's asset resolver, Ar. A path in scene content, such as the file of an image or a mesh, passes three stages.

1. **Asset path.** The string that a description authors: a TOML file, a MaterialX document, or a USD layer.
2. **Identifier.** The reader of the description anchors a relative asset path to the description's own resolved path, its anchor, with `createIdentifier`. A TOML reader anchors to `TXContext::getAnchor`, the MaterialX generator to the document. USD anchors its `asset` values itself.
3. **Resolved path.** The owner of the namespace resolves the identifier to a `ResolvedPath`. A file front end resolves to the identifier itself; hdFlux hands on the path that Ar resolved, by `HdFluxGetResolvedPath`.

The OS opens a resolved path that is a regular file, by `u8path`. Any other resolved path opens only through the `FileSystem` that the host passes to `Context::create`.

Past the reader, a resolved path never changes: code compares it, logs it, and opens it as it is. The core has no search path.

## Errors

- Scene content, such as a TOML file or a USD prim, describes the scene: the `Context` and its objects. The scene objects and their adapters, the CLI and hdFlux, follow the content rules.
- Every other API, such as a handler, a buffer, or a `Film`, takes its arguments from the calling code. Device code cannot throw, so only the host APIs check misuse that would corrupt memory.
