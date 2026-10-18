# Agent documents by task and place

## Context

Agents work in this repository in sessions that start with no memory, under a harness that may change. What a session must know has to be in the files that it loads. A long list of rules loads in full for every task, though each task needs only some of them.

## Decision

The [locality skill](../../.agents/skills/locality/SKILL.md) places each fact. The [docs skill](../../.agents/skills/docs/SKILL.md) says how to write each kind of document, and [agent documents](../agent-documents.md) gives where each kind lives in this repository. Hooks check what a script can check.

## Consequences

- A session loads the index of its place, then only the skills and documents that its task needs.
- A document that names the home of a value stays true when the value changes.
- The structure grows by adding files: a skill and its route for a new kind of task, an `AGENTS.md` and documents for a project when the first fact true only of it appears, and a decision record for a decision that binds later work.
- A skill holds in any repository, but a task may need the choices of its place as well. The route to the skill names both.
- A skill cannot point to examples in a project, so it carries its own.
- Every session depends on the skills and the `AGENTS.md` files, so a change to them needs more care than a change to a topic document.

## Alternatives

Evidence on deepseek-harness is in [its research document](../research/deepseek-harness.md).

- **Knowledge where it is loaded.** Each document could carry the facts that its readers need, so that a task loads fewer files. [Locality](../../.agents/skills/locality/SKILL.md) gives the cost of each copy.
- **A home per type of content.** deepseek-harness places a fact by its type, in a table of 13 tiers. A how-to fits two of them, cookbooks and skills.
- **A thin skill over a standard in `docs/`.** A skill could defer to a standard kept in `docs/`, so that an agent that edits documents without the skill still sees the standard. A route in each `AGENTS.md` names the tasks that need the skill, so an agent sees the skill when it applies. deepseek-harness chose the thin skill once, and now keeps its documentation and prose standards in skills, routed from its `AGENTS.md` files.
- **Every rule in the root `AGENTS.md`.** Every session loads every rule, whatever its task.
- **Choices of a place in a skill.** A skill could name the directories, scripts, and hooks of this repository, so that a task loads one file. The skill then holds a fact of a place, and is false wherever the place chooses differently.
- **The memory of a harness.** It reaches one harness only, and git does not version it.
