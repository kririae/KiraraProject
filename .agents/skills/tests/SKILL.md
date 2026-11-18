---
name: tests
description: Use when you write, change, or delete a test.
---

# Tests

A unit test locks one promise: a documented result or error, or a rule that the code must keep. Its value is a bug that it catches and nothing else would. Its cost is its lines and its setup, which every later change must keep working. A test earns its place, or it goes, and the default is no new test.

- A unit test earns its place where integration tests do not easily reach: the edges of a contract, failure states, behavior that depends on history, and [invariants](../design/SKILL.md).
- A mutation, such as a flipped condition, an off-by-one, a dropped clear, or a missing check, judges a test. Mutations are not part of the suite.
- A throwaway check verifies one change once: a run, a script, or a test deleted after it ran, reported with its command and its numbers. It protects nothing later.
- Trivial code gets no test, such as a rename, a forwarding call, or a log line. A missing test is better than a low-value one.
- Where the arithmetic allows, make the oracle exact by construction. Fill the inputs with small integers that the arithmetic holds without rounding, and compare for equality. A tolerance passes an error as large as itself.

## Steps

1. Read the tests of similar code in this project, and reuse their fixtures and their form.
2. List in your reasoning:
   - for each behavior that you add or change, a plausible mutation of it, and what kills it: a unit test, an integration test, or nothing
   - for each test that you add, change, or read on the way, the promise that it locks, and a plausible mutation that it kills and no test that runs by default kills
3. Add a unit test only where the code is not trivial and no test that runs by default kills a mutation of a promise, and extend an existing test before you add one. Delete each test for which you cannot list a promise and a mutation, and each one that you are not sure of.

## Bad tests

- A test that asserts a cache hit. It locks a strategy, not a promise.
- A unit test that checks what every end-to-end test already checks. Another test kills each of its mutations.
- A test that a bad argument throws, where the throw only checks a precondition. The check is not promised.
- A new fixture beside tests of the same class that share one. Every later change must keep both working.
- A comparison within 1e-5 of a reference, where small-integer inputs would make the result exact. A change that errs by less than 1e-5 passes.
