---
name: docs
description: Use when you write or edit Markdown for agents, such as AGENTS.md, files under docs/, or skills under .agents/skills/.
---

# Writing Markdown for agents

## Scope

Agent documents are the tracked files that match one of these:

- `AGENTS.md` in any directory.
- `docs/**/*.md` at the root or in any subproject.
- `.agents/skills/**/*.md`.

README files are not agent documents.

## Rules

- Write each paragraph as one physical line. A paragraph has no length limit. This holds for paragraphs in list items and block quotes too. Headings, code blocks and tables are exempt.
- Every link, image and reference definition must resolve to a tracked file or to a directory that holds tracked files. A fragment on a Markdown target must name one of its headings.
- Every agent document must be reachable from the root `AGENTS.md` by following links to agent documents. A link to a directory reaches nothing.

Run `git add` on a new file first, so that git tracks it.

## Verify

Run both from the repository root:

```sh
scripts/verify-md-wrap.py
scripts/verify-md-links.py
```

Each prints `<file>:<line>: ...` for every problem and exits 1.
