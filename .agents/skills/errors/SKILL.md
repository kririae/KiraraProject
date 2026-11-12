---
name: errors
description: Use when you decide how code handles a bad value, a misuse, or a failure.
---

# Errors

Code handles a bad value by where the value came from:

| Source | Handling |
|---|---|
| Content: authored data, such as a document or a configuration file | Respect what it asks for. Never invent a result it did not ask for: a value that asks for nothing produces nothing. Bound a value that asks for something unsupported to the nearest supported value, and warn. Report an unsupported feature the same way, and keep running. Content never crashes the process. |
| Calling code | A bad argument violates a precondition, and the caller answers for it. Checking it is optional. Misuse of a public API that would corrupt memory is always checked, and throws, where the code can throw. |
| Required configuration | Throw when it is missing and has no fallback. |
| A limit of our own that a request can reach, such as the capacity of a 32-bit index | Throw when a request goes beyond it. |
| The environment: a system API, the device, or memory | Propagate the failure, including a request beyond one of its limits. Log it and continue, or throw. Never terminate. |
| Our own code | Terminate, through an assertion or otherwise, only when an internal invariant broke. |

- Code that reads content is an adapter. Adapters, and the objects that hold content, follow the content rules. An adapter keeps its requests within the preconditions of the APIs it calls.
- Well-formed content has one definition, and all other content is handled the same way. Content that passes the checks made up front, but then fails to decode, is an environment failure.
- A legal edge value, such as an empty list, has a defined result and no warning.
- A resource is never empty: building an empty one violates a precondition, so an adapter turns empty content into no resource. A collection may be empty, and an optional attribute may be absent.
- A degenerate value is not empty. Keep it, and bound what it affects.
- An extremely rare case gets the handling that the table gives, and no machinery beyond it.
- A limit that no request reaches gets no check.
- A NaN or an infinity, whether arithmetic or content produced it, propagates without a check, unless it can make a memory access unsafe.
- On a path that is called repeatedly, each warning fires where a change of its input is detected, so a call with unchanged inputs warns about nothing.

## Steps

1. Before you choose how to handle a kind of bad content, find how the precedents and the [references](../design/SKILL.md) handle it, as [survey](../survey/SKILL.md) says.
2. List in your reasoning each bad value, misuse, or failure that the code handles, its source, and the handling that this skill gives it.
3. Make each handling match the list.

## Bad handling

- `if (width == 0) width = 1;` for a width read from a file. It invents a result.
- `catch (...) {}` around a driver call. It hides an environment failure.
- `assert(file.good())` after opening a file that the user named. Content crashes the process.
- A missing image skipped, and a corrupt one replaced by a placeholder. Each defect gets its own case.
- An empty mesh built for a shape with no points. An adapter turns empty content into no resource.
- A flag kept only to warn once. It is a special case for one warning, and a repeated warning is better.
- A check that a 64-bit ID counter has not wrapped. No request reaches the limit.
- A check for NaN in a sum that indexes no memory. The NaN propagates.
- An upload split in parts only to stay under a limit of the API. The failure of a request beyond the limit propagates.
