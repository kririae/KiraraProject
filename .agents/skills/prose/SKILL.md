---
name: prose
description: Use when you write a comment, a document, or a commit message.
---

# Prose

Write for a reader who has only the repository, as it is now. Use short, plain sentences with one idea each. Say less rather than more.

- Never join clauses with a semicolon. Write two sentences.
- Write parallel items as a `-` list, and ordered steps as a numbered list, rather than in one paragraph.
- Only a commit message describes a change.
- Define a new term where you introduce it, and give each concept one term, as [naming](../naming/SKILL.md) says.
- State what you checked as fact. Say what you did not check, and do not hedge.
- Write in the language of the repository, whatever language you use with the user.

## Check

1. List in your reasoning each term that you introduce, and where you define it. Define each term that has no definition a reader can reach.
2. Delete each sentence that a reader with only the repository cannot follow, such as one that cites a review, a plan, or a conversation.

## Bad prose

- "Bound the value; warn." A semicolon.
- "As decided in D3, the cache never evicts." The reader has no D3.
- "Rejected in review: caching the parsed file." The reader has no review.
- "The parser no longer stores the path." A change, outside a commit message.
- "Supports no compressed file yet." A temporary state.
- "This should be fast enough for most inputs." A hedge.
- "Job", then "task", then "request" for one concept. The reader looks for three concepts.
- One paragraph that packs results, errors, and blocking together. Parallel items belong in a list.
