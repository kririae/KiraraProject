# FLux Development Rules

## Questions

- A question is not a request. When the user asks a question, answer it and stop. The user asks
  because starting might go wrong, and decides from the answer whether to start.

## Design

- Review a feature's design before implementing it and again before delivering it: is it the
  simplest design, and does it fit the existing structure? Complexity it adds, such as duplicated
  state, a parallel code path, or extra failure handling, usually means a misplaced seam. Look for
  the seam that makes it disappear.

## Testing

- After a change, run only the unit tests it touches. Run the full suite only when asked.

## Input handling

- Validate scene input with an exception only when continuing could access invalid memory.
- Propagate failures from required system APIs.
- Bad scene values are not exceptional. Bound them to the supported domain and keep rendering.
- Do not add defensive NaN or infinity checks to rendering code. Let arithmetic propagate unless it can make memory access unsafe.
- A resource is never empty. Reject a mesh without triangles or an image without pixels when it is
  built. A collection may be empty, and an optional attribute may be absent. An adapter turns empty
  input into no resource.
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
