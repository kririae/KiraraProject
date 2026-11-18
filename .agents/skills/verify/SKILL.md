---
name: verify
description: Use when you say that a change works, or when you run a test, a benchmark, or an experiment.
---

# Verify

Before you say a change works, run the narrowest checks that could fail it, in bounded time. A check that you did not run proves nothing, and neither does one that could not fail.

- Build and run only the unit tests that the change touches. Run the full suite only when the user asks.
- Every process has a timeout. An experiment takes about a minute. Check whether the result already exists before you wait.
- Never run `sleep`, in any form. Wait in the foreground, or for the notification that comes when the process exits. A subagent runs every process in the foreground.
- Size the checks by the risk of the change. A change that very likely cannot break, such as a rename, a comment, or a constant that nothing reads, gets no dedicated test. The careful checks stay for concurrency, ordering, schedules, and numerics.
- Measure only what is in question.
- To compare two versions, build each with its own artifacts, and turn caches off.
- A correctness claim rests on the oracle that [tests](../tests/SKILL.md) defines. Where the arithmetic does not allow one, the report says why, and what bounds the error instead.
- A performance claim rests on one session. The two versions run interleaved, A B A B, so that the clock, temperature, and load of the device move both alike.

## Steps

1. List the checks that could fail the change, and state the expected time of each.
2. Run them, each with a timeout.
3. Report each check with its command and its result, each skipped check with its reason, and each failure with its output.

## Bad reports

- "All tests pass" after one test binary ran. It claims more than it ran.
- The full suite after a one-line change. It is not the narrowest check.
- A benchmark with no timeout. It may never end.
- "Sleep 600, then read the log." It waits for the worst case, and the result may have been there at once.
- "Verified" with no command. Nobody can repeat it.
- A speedup measured with a binary that loaded a library of another build. It compares the wrong artifacts.
- A speedup of two runs from two sessions. The device drifted between them, and the drift can be as large as the claim.
- A report that gives a tolerance where the oracle was exact, and does not say why.
