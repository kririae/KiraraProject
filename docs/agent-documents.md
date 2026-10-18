# Agent documents

Where each kind of document lives in this repository. The [locality skill](../.agents/skills/locality/SKILL.md) says what goes into them, and the [docs skill](../.agents/skills/docs/SKILL.md) how to write them. [ADR 0001](adr/0001-agent-documents.md) says why they are organized this way.

- The `AGENTS.md` of a place sits in its directory: the root, or a project directory.
- A skill is `.agents/skills/<name>/SKILL.md`, with `examples.md` beside it when it needs one.
- The `docs/` of a place holds its topic documents as `<topic>.md`, its decision records as `adr/NNNN-<topic>.md`, numbered from `0001`, and its research as `research/<topic>.md`.
- A work item keeps its files in `.scratch/<work item>/`. When the work lands, its directory moves to `.scratch/archive/`.
- The backlog of a place is `.scratch/backlog/<place>.md`, such as `flux.md`, and `repository.md` for the root.
- `tools/worktree-add.sh` sets up the `.scratch/` of each new worktree.

The hooks in `.pre-commit-config.yaml` check their format. To check the documents, `git add` the new ones and run `pre-commit run`.
