# deepseek-harness

How the deepseek-harness repository organizes the documents for its agents. Each source is a file at commit `d743267` of <https://github.com/deepseek-ai/deepseek-harness>, with the section that holds the claim.

## Placement

- Each fact has one home, "the tier whose job it is". A table gives 13 tiers, each with its job and what does not belong in it (`docs/AGENTS.md`, "The tier taxonomy: one home per fact").
- A fact is placed by its type: "bugs → postmortems; rationale → Agent Notes; procedures → cookbooks; type definitions → subsystems; package contracts → READMEs; standing orders → root `AGENTS.md` with a rationale link" (same section).
- Skills hold "Reusable workflows and specialized decision standards". Cookbooks hold "Step-by-step how-tos with numbered verify steps" (same section).

## Skills

- The documentation skill calls itself "The DeepSeek Harness documentation standard", in 2362 words (`.agents/skills/dsh-doc/SKILL.md`, "Summary", and `wc -w`).
- The prose skill "owns editorial judgment and required prose coverage" (`.agents/skills/dsh-prose-standard/SKILL.md`, opening paragraph). `docs/AGENTS.md` (opening paragraph) and the root `AGENTS.md` ("Conventions") route to it.
- An archived note of 2026-07-04 had chosen "a thin workflow skill, contracts in docs", because "a standard packed into SKILL.md is invisible to an agent that edits docs without invoking the skill" (`.agents/notes/archived/process/2026-07-04-doc-tiers-and-budgets.md`, "Decision" and "Alternatives considered"). An archived note is not "authority for current behavior" (`.agents/notes/README.md`, "Archiving and deletion").
- No skill names a model: `grep -liE '\b(claude|gpt-?[0-9]|opus|sonnet|haiku|gemini|deepseek-(chat|coder|reasoner|v[0-9]))\b' .agents/skills/*/SKILL.md` lists no file. Skills name roles instead: "the orchestrating agent does not translate: spawn a subagent" (`.agents/skills/dsh-translate-docs/SKILL.md`, "The whole-document path (new pairs)").
- For text a model reads, such as tool definitions: "Start with minimal context" (`.agents/skills/agent-experience/SKILL.md`, the opening list), "Delete obvious constraints", and "Say each fact once" (same file, "Tool definitions").

## AGENTS.md

- The root `AGENTS.md` holds "Standing orders: rules an agent needs in context in every session" (`docs/AGENTS.md`, "The tier taxonomy: one home per fact"). It has 1937 words (`wc -w AGENTS.md`), under a target of 1,960 (`docs/AGENTS.md`, "Wordcount Budgets").
- `CLAUDE.md` is a symlink to `AGENTS.md` (`AGENTS.md`, "Editing these instructions").

## Writing

- "Document current state. Keep history in commits, PRs, Agent Notes, postmortems, or scoped persistence records." (`docs/AGENTS.md`, "Writing rules")
- "One physical line per paragraph", checked by `verify-md-wrap` (same section).
- A test for prose that leaks the authoring session: "could a reader at HEAD, with no access to any session transcript, PR thread, or uncommitted draft, resolve every reference and verify every claim?" (`.agents/skills/dsh-trim-cot-leakage/SKILL.md`, "The one test")

## Not found

- A measurement of how the tiers or the skills change what agents do.
- How a harness that reads neither `AGENTS.md` nor `CLAUDE.md` finds the skills.
