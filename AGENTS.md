# FLux Development Rules

## Questions

- A question is not a request. When the user asks a question, answer it and stop. The user asks
  because starting might go wrong, and decides from the answer whether to start.

## Design

- Review a feature's design before implementing it and again before delivering it: is it the
  simplest design, and does it fit the existing structure? Complexity it adds, such as duplicated
  state, a parallel code path, or extra failure handling, usually means a misplaced seam. Look for
  the seam that makes it disappear.
- The scene is the source of truth, so its edits give the strong exception guarantee: an edit that
  throws leaves the Context unchanged. State derived from the scene, such as a backend, gives only
  the basic guarantee: after an exception it stays valid and destructible, and the caller recovers,
  for example by creating a new backend. Never add a rollback, a staging copy, or any other
  mechanism that keeps derived state, such as the OptiX scene, as it was before a failure.

## Testing

- After a change, run only the unit tests it touches. Run the full suite only when asked.
- A unit test locks one promise: a documented result or `\throw`, or a rule of this file.
- Do not test a strategy, or a conclusion that another test or a default integration test reaches.
- Prefer a missing test to a low-value one. Delete low-value tests.

## Input handling

- Respect what the user asks for in the scene, and report misuse by the calling code. Two layers
  meet here:
  - Scene content, such as a TOML file or a USD prim, describes the scene: the `Context` and its
    objects. Scene objects and adapters, meaning the code that reads scene content, such as the
    CLI and hdFlux, follow the scene rules below.
  - Every other API, such as a handler, a buffer, or a `Film`, takes its arguments from the calling
    code. It follows the calling-code rules below.
- Scene rules:
  - Never invent a result that the user did not ask for. A value that asks for nothing, such as
    zero samples or a zero resolution, renders nothing. An adapter turns empty input into no
    resource.
  - Bound a value that asks for something unsupported to the nearest supported value, and warn.
    Report an unsupported feature the same way. The renderer keeps rendering.
  - An adapter keeps its requests within the preconditions of the APIs it calls.
- Calling-code rules:
  - A bad argument violates a precondition. State it as `\pre`.
  - Misuse at a host API boundary that would corrupt memory is always checked and throws
    `std::invalid_argument`. Other preconditions may be checked. No test locks such a check.
- `\throw` documents only an environment failure, missing required configuration, one of our size
  limits (`std::length_error`), or a check that is the function's purpose.
- Throw when required configuration has no fallback, such as an unset active integrator.
- Propagate failures from required system APIs, including a request beyond one of their limits.
  Do not add a feature, such as splitting work, only to avoid such a failure.
- Add no machinery for a case that is extremely rare.
- Scene content never crashes the process. Terminate, through an assertion or otherwise, only when
  our own code is wrong: an internal invariant broke. A failure from outside the code, such as a
  system API, the device, or memory, never terminates the process. Log it and continue, or throw.
- Do not check for NaN or infinity, whether arithmetic or the scene produced it, or for a 64-bit
  counter that wraps. Let them propagate unless they can make memory access unsafe.
- A resource is never empty: building an empty one violates a precondition. A collection may be
  empty, and an optional attribute may be absent.
- Degenerate values are not empty. Keep a zero-area mesh and bound what it affects.

## Naming

- Name an entity for what it is, in the established vocabulary of its domain, as a designer of
  the C++ standard library or LLVM would.
- Define domain terms clearly, then use them consistently.
- Do not repeat context that the type or scope already supplies.
- Do not name a value after the strategy that produced it, and do not reuse a name whose
  standard meaning differs.

## Comments

- Prefer invariants: state what always holds for a member, a container, or an index before
  describing what code does.
- A comment on an interface states its contract: what an entity is or does, its preconditions,
  results, and errors. It does not say how, name private members or the functions it calls, or
  mention caching or other strategies a caller cannot observe.
- An implementation comment says why, where the code does not show it. It never narrates a
  change.
- Comments and commit messages never join clauses with a semicolon. Write separate sentences.

## Commits

- A commit message is one descriptive Conventional Commits subject line, optionally followed by
  one paragraph of at most three lines. Nothing else, including trailers.

## Development Workflow

Development is stack-based, in the style of stacked diffs. The history is what the user reviews,
so each commit is a self-contained change sized for one review, with its code, tests, and docs. A
commit is corrected in place. The stack never holds a commit that fixes or follows up another
commit in it.

Work on the stack on a detached HEAD, managed with git-branchless. Each step of the work moves
through three states:

1. **Implement.** Start from the tip of the stack, or from the target branch for a new stack.
   Build the step as one commit. Then stop.
2. **Review.** The user reviews the step. Stage the fix and run `git absorb` to make fixup
   commits, then fold each into its target with `git move --fixup`. The step stays in review
   until the user approves it.
3. **Approved.** The step stays in the stack, and a later fix still amends it. The next step
   returns to Implement.

A feature lands as a whole. When the user says it is complete, move the target branch to its last
commit with `git branch -f`.

Throughout, rewrite history in memory with `git amend`, `git reword`, and `git move`, never with
`git rebase`. Never rewrite a commit that the target branch's upstream contains. Push only when
asked.

## Context identity

- Context IDs only increase. They identify objects and serve as lookup keys.
- Backend and device indices come from explicit maps. They have no numeric relation to Context
  IDs, so a Context ID is never an index.
