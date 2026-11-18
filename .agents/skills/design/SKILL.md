---
name: design
description: Use when you design a change, or choose its structure.
---

# Design

Start from code that solves a similar problem. Such code in this project is a precedent, and reliable code outside it is a reference. Differ from them only for a concrete reason.

A design rests on a mental model and the invariants of its parts. The model is a few sentences from which a reader can predict what each part does. An invariant is what always holds for the state of a part.

Give each piece of state, each guarantee, and each convention one owner. Two owners drift apart, and without an owner every caller redoes the work. The boundary between two owners is a seam. When no name fits a part, the part owns more than one thing.

After the second fix to one mechanism, stop patching, and design that mechanism again from step 1.

## Steps

1. Find the precedents and the references, as [survey](../survey/SKILL.md) says.
2. Design. Make it run before you make it complete, and record what you leave out as an [open task](../locality/SKILL.md). Write the design as numbered decisions: the question, the options, the choice, and why. List each new class with what it owns, and each new public name with its naming entry, as [naming](../naming/SKILL.md) says.
3. Check the design, and write down each result:
   - The mental model, and the invariants of each part. If you cannot write them, the design is wrong, so design again from step 2.
   - Each difference from a precedent or a reference, with its reason, and each decision that neither answers. Where a difference has no concrete reason, follow the precedent or the reference.

## Bad checks

- "The design is simple and fits the existing structure." It names no model and no invariant.
- "The cache stays valid." It does not say valid against what. An invariant says what holds: each entry matches the file at its path as of the last load.
- "We differ from the reference because our needs differ." The reason is not concrete.
- A comparison that omits a difference, such as a thread per task where the project and every reference use a pool. The difference goes unexamined.

## Bad designs

- An image size kept in the image and again in each reader. Two owners.
- A library function that takes a type of the application above it. The application should adapt.
- Checks for one host application's conventions in ten files. One adapter should own them.
- A copy of a search index, kept to restore it after a failed rebuild. Only the source of truth must survive a failure. The index is rebuilt.
- A lock around calls that the library already serializes. The library owns that guarantee.
- A third patch to the same lifetime bug, with no new design after the second.
- `SceneManager` for a class that loads files, caches images, and schedules renders. No name fits, because it owns three things.

