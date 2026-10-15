# KiraraProject

KiraraProject is a monorepo of graphics projects and the infrastructure they share:

- `flux/`: [FLux](flux/AGENTS.md).
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

## Code

- Before you write or change code, a build file, or a script, follow [code](.agents/skills/code/SKILL.md).
- Before you write, change, or delete a test, follow [tests](.agents/skills/tests/SKILL.md).
- Before you decide how code handles a bad value, a misuse, or a failure, follow [errors](.agents/skills/errors/SKILL.md).

## Documentation

- Before you write a comment, a document, or a commit message, follow [prose](.agents/skills/prose/SKILL.md).
- Before you write, move, or delete a document, follow [docs](.agents/skills/docs/SKILL.md), and read [agent documents](docs/agent-documents.md).
- Before you write or change a comment in code, follow [comments](.agents/skills/comments/SKILL.md).

## Verify

- Before you say that a change works, or when you run a test, a benchmark, or an experiment, follow [verify](.agents/skills/verify/SKILL.md).
- Before you review a step of the stack, or the fixes to one, follow [review](.agents/skills/review/SKILL.md).

## Commit

- Before you create, change, reorder, or land a commit, follow [commit](.agents/skills/commit/SKILL.md), and read [workflow](docs/workflow.md).
- Before you create or remove a worktree, read [workflow](docs/workflow.md).
