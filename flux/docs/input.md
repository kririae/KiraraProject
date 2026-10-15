# Input

How FLux applies the [errors skill](../../.agents/skills/errors/SKILL.md) to its input.

- Scene content, such as a TOML file or a USD prim, describes the scene: the `Context` and its objects. The scene objects and their adapters, the CLI and hdFlux, follow the content rules.
- Every other API, such as a handler, a buffer, or a `Film`, takes its arguments from the calling code. Device code cannot throw, so only the host APIs check misuse that would corrupt memory.
