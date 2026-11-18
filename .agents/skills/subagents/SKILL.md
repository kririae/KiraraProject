---
name: subagents
description: Use when you hand work to another agent, or act on what one returns.
---

# Subagents

You keep the judgment, and subagents do the work. Keep your own context for judging and checking, and hand out any task that needs much context. Work goes out as a brief and comes back as a report. Each step of the work then moves through the states that [commit](../commit/SKILL.md) gives.

- An advisor judges a question that you bring to it.
- A committee is two or more advisors and you. It takes a question with pitfalls, or one where you are not sure that every detail can be solved. Work starts only when every member, you included, reaches the same conclusion.
- An executor does the agreed work, and stops to object when the design forces complexity into the code, as [code](../code/SKILL.md) says.
- A reviewer reviews work, as [review](../review/SKILL.md) says.
- A scout gathers facts, as [survey](../survey/SKILL.md) says. Any subagent may send scouts, to keep its own context small. A scout sends no one.

Match the model to the role: the strongest for advisors and reviewers, a capable one for executors, and a fast one for scouts.

## Steps

1. Decide the work, with an advisor or a committee when it has pitfalls. Batch related work into one task.
2. Write the brief, a file kept with its [work item](../locality/SKILL.md). It holds all that the subagent needs: the task, what is ruled out, the skills to follow, and where to write the report. Hand it out in units of about five minutes of the subagent's work: after each, the subagent reports and stops, and you send the next. Size each unit by how long the last one took.
3. Check the report at its sources, as survey says. Take an objection to the design back to step 1. For a review, list in your reasoning a verdict on each finding: accepted or rejected, with a reason. Not every finding needs a fix. Send the fixes and your rejections back to the same reviewer, until it approves.
4. From the third review round, take structural findings to an advisor, as a question about the design. Fix execution findings.

An advisor may run an experiment that cannot disturb other work, and shares the result with its evidence. An experiment that can, such as a build in a shared tree or a timing run on a busy device, comes to you. Give it to an executor in its own worktree, and queue the timing runs that share a device.

Keep the sources in what you report to the user.

## Bad delegation

- A brief that says "as discussed". The subagent was not there.
- "Every mutation is killed", relayed to the user without a check. A report is a claim until checked.
- Review findings forwarded to the executor without a verdict on each. The judgment stays with you.
- A large change made in your own context. It fills the context that judging needs.
- A plan with pitfalls executed on the first answer of one advisor. Nobody else reached the conclusion.
