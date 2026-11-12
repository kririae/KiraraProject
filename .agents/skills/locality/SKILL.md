---
name: locality
description: Use when you write anything that states a fact or a rule, in code, a comment, a test, a document, a skill, or a memory, or when you decide where it belongs.
---

# Locality

Each fact and each rule has one home, where it is decided. A constant, a check, a signature, or a configuration decides its own value and behavior. An entity in the code decides its own invariants. Everywhere else, name the home, or leave the fact out.

A name costs the reader one more lookup. A copy costs nothing to read, but every later change of its home must find it, and a change that misses it leaves it wrong without notice. An agent reads cheaply and misses copies, so trade reading for maintenance: follow names, and write no copies.

Fewer copies is a means, not the goal. The goal is that no copy goes wrong without notice.

- A copy may stay when a check fails if it differs from its home, such as a test that asserts the value.
- A copy may stay in the same file as its home, since nobody changes a file without reading it.
- No machinery is added only to remove such a copy.
- A name carries no value: "the word limit that the hook checks", not the limit.
- A memory keeps only what has no home in the repository.
- What you learn goes into its home when you learn it.
- A problem that you find and do not fix becomes an open task. Record it, as step 1 says, and never only mention it.

## Steps

1. Ask who decides it, in this order, and stop at the first answer:
   1. A work item, a piece of work that the user asked for, decides its plan, its briefs, and its reports. Keep them with the work item, outside version control. When the work lands, move each finding that still holds to its home. Move each open task, a task left for later, to the work item that will do it, or else to the backlog of its place.
   2. The agent, the harness, or the machine that runs the work decides such things as its model, its local paths, and the language of its replies. Keep them in its configuration.
   3. The code and its configuration decide what each of their entities is and does: a value, a type, a function, a check, or a build target. Say it there, in a name, a type, a check, or a comment.
   4. A method that holds in any repository decides how to do a kind of task. Put it in a skill.
   5. A place, the repository or one of its projects, decides its own choices, and keeps the reasons and the evidence for them. If every task there needs it, make it an order in the place's `AGENTS.md`. Otherwise put it in the place's documents.
2. Split what mixes a method with the terms of a place. The method goes to its skill, and the place applies it in its own terms. Knowledge starts in the narrowest place it is true of. It moves up when it becomes true of a second place.
3. List in your reasoning each fact or rule that you write outside its home, with its home, and the check or the file that lets the copy stay. Replace each other copy with a route or a name, or leave the fact out.

## Bad copies

- "It has at most 900 words" in a guide, while the constant of a script decides the limit. The constant changes, and the guide stays wrong.
- "The server is written in C++23 and built with CMake" in an `AGENTS.md`. The build and the code decide those facts.
- An invariant of a class stated in a design document, and not on the class. The class changes, and the document does not.
- A memory that restates a rule of a skill. One of the two goes stale.
- One rule in two skills. A change finds only one of them.
- A rule of one project in a skill, or a rule for every project in the documents of one project. It is false, or unread, where it does not belong.
- A finding left with its work item after the work landed. Nobody reads the archive.
- A shared module of constants, added only so that a test does not repeat the value it asserts. The test already fails when the two differ.
