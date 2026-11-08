---
name: comments
description: Use when you write or change a comment in code.
---

# Comments

Code comes first, and comments second. A comment states facts, so [locality](../locality/SKILL.md) and [prose](../prose/SKILL.md) are its rules, and it states only what the code it sits on decides. A name, a type, or the structure of the code cannot drift from the code, but a comment can go wrong without any check failing.

Say an idea in code when you can:

- in a name, and in the interface: its types and signatures
- in the structure of the code: its functions, its blocks, and the blank lines between them

Comment only what the code cannot say:

- on an interface, the contract, the invariants, and the mental model, as [design](../design/SKILL.md) defines them
- beside a highly non-trivial block, what the block does, when reading the block takes much more effort than reading the comment

## Shape

A comment on an entity has these paragraphs, in this order, and nothing else. Each one is optional, and a comment with none is the usual case.

1. A summary: one sentence, short enough to read at a glance. If it is not short, or you do not find it easy to read, delete it.
2. The invariants, only those that the code does not state and a caller or a maintainer must keep.
3. A short model, only where the invariants do not let a reader predict what the entity does.
4. A special behavior that a caller can rely on, only where there is one.

A comment never says why. A reason is not a fact that the code decides, and it goes stale without any check failing.

## Check

For each new or changed comment, list in your reasoning each thing that it states, and then:

1. If a name, the interface, or the structure of the code can say it, say it there, and delete it from the comment.
2. If the code that the comment sits on does not decide it, delete it. A header, for example, decides neither a value that its source file chooses, nor a behavior that its contract does not promise.
3. If you are not sure whether to keep it, delete it.
4. If it lists the members of a set that other code defines, such as the functions that a backend provides or the cases of an enum, delete the list. It must change with every member, and no check fails when it does not.
5. If a change to other code makes you edit a comment, or you change a comment that an earlier change already edited, it states what other code decides. Delete it, however old it is.

## Bad comments

- "The timeout in milliseconds" on an `int timeout`. A type such as `std::chrono::milliseconds` says it.
- "Must not be copied" on a class. `= delete` says it, and the compiler checks it.
- "Phase 2: merge the runs" in the middle of a long function. A function named `mergeRuns` says it.
- "Runs on a pool of eight threads", in a header, where the source file picks eight. The source file decides the value.
- "Returns the entries in insertion order", where the contract promises no order. Callers will rely on it.
- "Throws if the pointer is null", where checking is optional. The check is not promised. State a precondition.
- "A negative timeout becomes 0." How a bad value is bounded is not promised. "An empty batch does nothing" is a promise.
- "Sends nothing to a user who opted out." The opt-out rules live elsewhere.
- "Flushes on destruction" on one implementation of a writer interface, when every implementation must. The interface decides it.
- "We do not retry here." No code decides it.
- "Increment the retry count." Reading the code is as fast as reading the comment.
- A summary of two sentences, or one that wraps over three lines. It is not short, so it goes.
- "A backend provides `flux_context`: `ShadingContext`, `fluxGetWorldNormal`, `fluxGetTexCoord`, `fluxToShadingFrame`, and `fluxSampleTexture`." on a generator. Three commits edited the list as the backend grew.
- "Why: the cache is cleared so that stale entries cannot be read." A comment never says why. The invariant says what holds: every entry matches its file.
- The compare-and-swap loop of a lock-free queue, with no comment on what each step expects. Reading it costs far more than one sentence.
