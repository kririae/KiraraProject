---
name: commit
description: Use when you create, change, reorder, or land a commit.
---

# Commits

The history is what the user reviews. Work is a stack of steps on a target branch, the branch that the stack lands on. Each step is one commit: a self-contained change sized for one review, with its code, tests, and documents. A step is corrected in place, so the stack never holds a commit that fixes or follows up another one. The stack reads as if its final design was decided at once.

- A commit makes one change. A rename and a feature are two steps.
- A message describes the change against the target branch, as [prose](../prose/SKILL.md) says. It never mentions a rejected option, an earlier version of a step, or a label of a plan.
- A feature lands as a whole.
- A commit that the upstream of the target branch contains is never rewritten. Nothing is pushed unless the user asks.

## Steps

A step moves through four states:

1. Implement. Build it as one commit, on the tip of the stack, or on the target branch for a new stack.
2. Agent review. An agent that did not write the step reviews it, until the agent approves. One review may cover several steps.
3. User review. The user reviews it, until the user approves.
4. Approved. It stays in the stack until the feature lands.

A review reads the commits, so the next step may start while a review runs. A fix found in any state amends the step it concerns. After a tool rewrites the stack without running the commit hooks, run them at each rewritten commit before it goes to review. When the user says that the feature is complete, move the findings and open tasks of its work item, as [locality](../locality/SKILL.md) says, and then move the target branch to its last commit. A finding whose home is in the repository moves in a step of the stack.

## Bad commits

- `fix: address review comments` on top of the step it fixes. A fix amends its step.
- One commit that renames a type and adds a feature. It makes two changes.
- `feat: add step 2 of batch B`. The reader of the history has no plan.
- A message that mentions an earlier version of the step. The reader sees only the final one.
- A step handed to the user before an agent reviewed it. It skips a state.
- A stack rewritten in memory and sent to review without running the hooks. A rewrite runs no hook.
- A target branch moved while the work item of the feature still holds its findings. Nothing after landing leads back to them.
