# Architecture

How FLux applies the [design skill](../../.agents/skills/design/SKILL.md) to its layers, its references, and its scene.

## Layers

- FLux core never depends on Hydra. Its API ends at core types, such as `Film`, and hdFlux adapts them to Hydra.
- hdFlux is a general Hydra delegate. Houdini is its first host application. hdFlux adapts to the convention of a host in one place, and names the host there.
- hdFlux follows the structure of hdEmbree, the example delegate of USD.

## References

- For Hydra, consult the references in this order: hdEmbree, Cycles, hdPrman, and arnold-usd.
- For rendering behavior outside Hydra, such as how to handle bad content, consult Cycles, Arnold, RenderMan, Karma, OpenImageIO, pbrt, and Mitsuba.

## Scene

- The scene, meaning the `Context` and its objects, is the source of truth. An edit of the scene that throws leaves the scene unchanged.
- A backend, such as `OptixHandler`, holds state derived from the scene. After a failure, a backend stays valid and destructible, and its caller recovers, for example by creating a new backend. A backend keeps no rollback, staging copy, or other means to return to its state before a failure.
