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

## Workflow

Development is stack-based, in the style of stacked diffs. The history is what the user reviews, so each commit is a self-contained change sized for one review, with its code, tests, and docs. A commit is corrected in place. The stack never holds a commit that fixes or follows up another commit in it.

Work on the stack on a detached HEAD, managed with git-branchless. Each step of the work moves through three states:

1. **Implement.** Start from the tip of the stack, or from the target branch for a new stack. Build the step as one commit. Then stop.
2. **Review.** The user reviews the step. Stage the fix and run `git absorb` to make fixup commits, then fold each into its target with `git move --fixup`. The step stays in review until the user approves it.
3. **Approved.** The step stays in the stack, and a later fix still amends it. The next step returns to Implement.

A feature lands as a whole. When the user says it is complete, move the target branch to its last commit with `git branch -f`.

Throughout, rewrite history in memory with `git amend`, `git reword`, and `git move`, never with `git rebase`. Never rewrite a commit that the target branch's upstream contains. Push only when asked.

Create a worktree only with `tools/worktree-add.sh` and remove it only with `tools/worktree-remove.sh`. Never run vcpkg in it.

A commit message is one descriptive Conventional Commits subject line, optionally followed by one paragraph of at most three lines. Nothing else, including trailers.

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

- After a change, run only the unit tests it touches. Run the full suite only when asked.
