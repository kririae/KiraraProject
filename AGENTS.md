# KiraraProject

KiraraProject is a monorepo of graphics projects and the infrastructure they share:

- `flux/`: FLux, a renderer.
- `kira/`: KIRA, the library that the other projects share.
- `kirara-dance2/`: the rewrite of Kirara Dance.

Before you work on a file, read every `AGENTS.md` from the root down to its directory.

Before you write anything that states a fact or a rule, in code, a comment, a test, a document, a skill, or a memory, or when you decide where it belongs, follow [locality](.agents/skills/locality/SKILL.md).

A question is not a request. When the user asks a question, answer it and stop. The user asks because starting might go wrong, and decides from the answer whether to start.

## Decide

- Before you design a change, or choose its structure, follow [design](.agents/skills/design/SKILL.md).
- Before you name an entity, such as a type, a function, a variable, a file, or a test, follow [naming](.agents/skills/naming/SKILL.md).
- Before you gather facts from code, documents, or other repositories, or send a scout to gather them, follow [survey](.agents/skills/survey/SKILL.md).
- Before you hand work to another agent, or act on what one returns, follow [subagents](.agents/skills/subagents/SKILL.md).

## Design

- The scene is the source of truth, so its edits give the strong exception guarantee: an edit that throws leaves the Context unchanged. State derived from the scene, such as a backend, gives only the basic guarantee: after an exception it stays valid and destructible, and the caller recovers, for example by creating a new backend. Never add a rollback, a staging copy, or any other mechanism that keeps derived state, such as the OptiX scene, as it was before a failure.
- FLux core never depends on Hydra. Its API ends at core types such as `Film`.
- hdFlux is a general Hydra delegate. Adapt to a host's convention in one place, and name the host there.
- Look a name up in the C++ standard library, LLVM, CUDA, and USD.
- Prefix CUDA wrappers with `Cuda` and OptiX backend types with `Optix`.
- Share a class's internals through an internal `XPImpl.h` header, not through accessors.
- Context IDs only increase. They identify objects and serve as lookup keys.
- Backend and device indices come from explicit maps. They have no numeric relation to Context IDs, so a Context ID is never an index.

## Documentation

- Before you write a comment, a document, or a commit message, follow [prose](.agents/skills/prose/SKILL.md).
- Before you write, move, or delete a document, follow [docs](.agents/skills/docs/SKILL.md), and read [agent documents](docs/agent-documents.md).
- Before you write or change a comment in code, follow [comments](.agents/skills/comments/SKILL.md).

## Code

- Before you write or change code, a build file, or a script, follow [code](.agents/skills/code/SKILL.md).
- In FLux, scene content, such as a TOML file or a USD prim, describes the scene: the `Context` and its objects. The CLI and hdFlux are its adapters. Every other API, such as a handler, a buffer, or a `Film`, takes its arguments from the calling code. Device code cannot throw, so only the host APIs check misuse that would corrupt memory.
- State a bad argument as `\pre`. A checked misuse throws `std::invalid_argument`, and a lookup of an absent ID or key throws `std::out_of_range`, as `std::map::at` does. Document an error as `\throw`. One of our size limits throws `std::length_error`.
- Before you write, change, or delete a test, follow [tests](.agents/skills/tests/SKILL.md).
- Before you decide how code handles a bad value, a misuse, or a failure, follow [errors](.agents/skills/errors/SKILL.md).

## Verify

- Before you say that a change works, or when you run a test, a benchmark, or an experiment, follow [verify](.agents/skills/verify/SKILL.md).
- Before you review a step of the stack, or the fixes to one, follow [review](.agents/skills/review/SKILL.md).

## Commit

- Before you create, change, reorder, or land a commit, follow [commit](.agents/skills/commit/SKILL.md), and read [workflow](docs/workflow.md).
- Before you create or remove a worktree, read [workflow](docs/workflow.md).
