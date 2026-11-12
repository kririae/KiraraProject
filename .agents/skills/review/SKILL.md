---
name: review
description: Use when you review a step of the stack, or the fixes to one.
---

# Review

A review runs the checks of the skills that govern a step again, by an agent that did not write the step. It reads the step as the user will: the commits under review, each header before its implementation, and never a working tree, which may already hold later work.

- A finding names a location, the rule broken, and how it fails.
- A finding about a misplaced seam names the seam to move, not only the symptom.
- A hook already checks what it checks. Taste that no skill states is not a finding.

## Steps

1. Find each promise that the step breaks, as [tests](../tests/SKILL.md) defines promises.
2. Run the checks of each skill that the work needs: [design](../design/SKILL.md), [naming](../naming/SKILL.md), [code](../code/SKILL.md), [errors](../errors/SKILL.md), [tests](../tests/SKILL.md), [comments](../comments/SKILL.md), [prose](../prose/SKILL.md), [locality](../locality/SKILL.md), [docs](../docs/SKILL.md), and [commit](../commit/SKILL.md). Run each check yourself, since an author lists most results only in its reasoning. Where the author wrote results into the work, such as the checks of a design or the complexity that a report of code lists, compare them with yours.
3. Read the whole stack for what one step does not show: a class that gains a duty in each step, or a fix that touches the code of an earlier fix.
4. Write the report. Mark each finding structural, meaning a seam, ownership, a wrong mechanism, or a fix that recurs, or execution, meaning a comment, a name, a test, or formatting. End with a verdict.

On a later round, check each fix, and each rejected finding against the reason given.

## Bad findings

- "Consider simplifying this class." No rule and no failure.
- A fix for the symptom of a misplaced seam. The seam stays where it was.
- "The tests look good", with no mutation named.
- A finding about a file in the working tree that the step does not contain. It reviews later work, not the step.
- A design accepted because it lists its differences from the references, when one of its reasons is not concrete. A list is not a check.
