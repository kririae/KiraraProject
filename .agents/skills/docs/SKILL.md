---
name: docs
description: Use when you write, move, or delete a document.
---

# Documents

[Locality](../locality/SKILL.md) decides what belongs in a document, and in which one. This skill says how to write each kind. A document owns one thing, and each of its sentences follows from what it owns. A sentence that follows from something else belongs to the document that owns that thing.

- An `AGENTS.md` is the index of its place. It holds what a session needs before it knows its task: one sentence on what its place is, the orders that locality places there, and routes. A route names a task and what to load for it: "Before you X, follow Y" for a skill, and "read Y" for a document.
- A skill holds how to do one kind of task, in any repository. Its front matter gives its `name`, which is the name of its directory, and a `description`, "Use when you X.", where X is the task of its route. It links only to skills and to outside sources, and to each skill it builds on.
- A topic document describes one topic of its place.
- A decision record (ADR) gives one decision in force, in four sections: Context, Decision, Consequences, and Alternatives. When the decision changes, change its record, and move the old choice to Alternatives with the reason it lost.
- A research document keeps evidence, written as a [survey](../survey/SKILL.md) report.
- A `README.md` is for people.

Every document is reachable from the root `AGENTS.md`. A decision record or a research document is linked from the documents, other than skills, that rest on it. A change to an `AGENTS.md` or a skill lands only after the user reviews it.

## Skills

A skill has four parts:

1. Its few principles, under its title. Prefer an example of a principle to a new rule.
2. The steps of the task, in the order an agent takes them, with the checks known to be useful. A check names what the agent lists, and what it does when the list fails, such as "if you cannot list it, delete it". At most three checks ask the agent to list their results, in its reasoning or in the work.
3. Links to the skills that own the rest of the task.
4. `## Bad <things>`: generic examples of its principles, each with its fault.

## Steps

1. Find the document that owns what you write, as locality says.
2. Write it, as [prose](../prose/SKILL.md) says.
3. List in your reasoning, for each sentence that you write in a skill, the principle it follows from. Move a sentence that follows from what another document owns, and delete one that follows from nothing. A change to a skill deletes at least as many words as it adds.

## Bad documents

- "Before you change `Parser`, read ..." A route names a task, not a class.
- A skill that sends the reader to a project's files. It is false in any other repository.
- Rules for choosing a name in a skill on design. Choosing a name is a task of its own, not a choice of structure.
- Error handling as the whole of a skill named for code. The name promises how to write code.
- "A film shape stored in a pool entry" as an example in a skill. A reader outside the project does not know the terms.
- A rule that the principles of its skill already imply. It is a second home for the rule.
