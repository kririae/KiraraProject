---
name: survey
description: Use when you gather facts from code, documents, or other repositories, or send a scout to gather them.
---

# Survey

A survey replaces memory and framing with facts that cite their sources. It reports what the sources say, and never judges. Whoever asked for it judges. An agent that surveys for another is a scout.

- Code shows what exists now, not what a project is for or will become. Never infer a purpose from it.
- Read only. Never run code from another repository.
- A scout gets a light task: a few sources, and one table as its output. Several scouts with light tasks beat one with a broad task.

## Steps

1. Before you decide, list the facts that the decision rests on.
2. Read each fact at its source, or send scouts for them, and merge their reports yourself.
3. Write the report, as [prose](../prose/SKILL.md) says:
   - Cite each claim by a file and a symbol or section in it, by a URL, or by the command that produced it, never by a line number.
   - End with a "Not found" section: what you looked for and did not find, and what you did not read.

Before a design, a brief, or a report goes out, check at the source each fact that it rests on.

## Bad reports

- "zlib probably checks the length first." No source.
- "We should do what SQLite does." A judgment.
- "This project is a collision-detection library." A purpose read from the first module of a rewrite.
- A summary of a file the scout did not open. It cites nothing that was read.
- "See `parser.cpp:212`." The line moves with the next edit.
