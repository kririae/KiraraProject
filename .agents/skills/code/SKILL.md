---
name: code
description: Use when you write or change code, a build file, or a script.
---

# Code

Code tests its design. Where the design forces complexity into the code, fix the design, not the code, since a small change to a design often removes much code. When you find such complexity, stop, and report it to whoever decided the design, with the code that another decision would remove. Such a report finishes the task.

- Write each mechanism, such as the ownership of a resource or a dispatch over a closed set of types, as its [precedents and references](../design/SKILL.md) write it, in the idioms of its language. A project writes each mechanism one way.
- Each check, branch, flag, lock, and layer serves a [promise](../tests/SKILL.md). What no promise needs goes.
- Write `auto` where the right-hand side names the type, as in `auto const shape = Shape::make(x, y, z);`. Spell the type where the right-hand side does not show it, such as the call of a free function whose result type matters to the reader.
- Code is read more often than it is written. Keep complex logic near the left margin, keep each function at one level of detail, and mark the paragraphs of a function or a class with blank lines, braces, and access specifiers. What the code cannot say goes in a [comment](../comments/SKILL.md).

## Steps

1. Read the code around the change, and the code in this project that solves a similar problem. For each mechanism that you will write, find its precedents and references, and its idiom in the language, as [survey](../survey/SKILL.md) says.
2. Write the code. Name each entity as [naming](../naming/SKILL.md) says.
3. Check the code that you add or change:
   1. In your report, list each check, special case, flag, lock, layer, and copy of state or of logic, with the promise that needs it, and the design decision that forces it, if one does. Delete what no promise needs, and what you are not sure a promise needs. Where another decision would remove it, or the references do without it, stop, and report that decision.
   2. In your reasoning, list each mechanism with its precedent, its reference, or its idiom. Rewrite a mechanism that differs from them, unless you can state a concrete reason. Where a precedent differs from another precedent or from the idiom, fix it in a commit of its own, or record it as an [open task](../locality/SKILL.md).
   3. In your reasoning, list each block nested more than two levels deep, and why an early exit, a `continue`, or a function cannot flatten it. Flatten each block for which you have no reason.

## Bad workarounds

- A flag that tells a function which of its two callers called it. The function does two jobs.
- A parameter passed through three layers that never read it. The state that it carries has the wrong owner.
- A value converted at a boundary, and converted back just past it. The seam sits on the wrong side.
- A check in every method for a state that another design would make unreachable. The design should remove the state, not guard it.
- A class that a test cannot build without half the system. It owns, or needs, too much.
- A workaround that meets the brief, with no word of it in the report. The design never learns its error.

## Bad idioms

- A constructor that releases its handles in a catch block after each step that can fail. Delegate first to a constructor that leaves the object empty, owning nothing. The destructor then releases the handles, once.
- A catch block that releases a handle, in a constructor that delegates to one that leaves the object empty. The destructor runs too, and releases the handle again.
- Move assignment written one way in two classes, and another way in two more. A reader learns two idioms for one job, and a fix to one misses the others.
- A move assignment that releases the old resource by hand. Copy-and-swap leaves the release to the destructor.
- A second owning class for a kind of resource that a class already owns. Each copy implements its moves again.
- A `switch` on a type tag beside a `std::variant`. `std::visit` with an overload set fails to compile when an alternative has no case.
- An `init()` that must run after the constructor. The constructor makes the object usable, or throws.
- An `open()` that callers must call before `read()`. Return the readable object from `open()`, and the order cannot break.
- `Shape const shape = Shape::make(x, y, z);` The right-hand side names the type.
- `render(true, false)`. An enum names each choice.
- `bool hasTimeout; int timeout;` A flag beside a value. A default, or `std::optional`, says it.
- The same member and accessor copied into five classes. A mixin owns them once.

## Bad machinery

- A null check on a pointer that the constructor guarantees. The invariant makes it unnecessary.
- A cache cleared on every change of its inputs, except one. One rule covers every case.
- A second call with unchanged inputs that rebuilds the whole index. Unchanged inputs need no new work.
- Two functions that differ in one constant. One function takes the constant.

## Bad structure

- `if (ok) {` forty lines `} else { return error; }`. Return early, and keep the main path at the left margin.
- An `else` after a `return`. It nests the rest for nothing.
- A search loop inside a loop inside a loop. A function named for what the inner loops find flattens it.
- A function that reads a file and blends pixels in the same loop. A function works at one level of detail.
- A function that parses, checks, and renders with no blank line between the steps. Its paragraphs run together.
- A build file with no blank line between its targets. Its blocks run together.
- A guard that holds a lock to the end of a long function. Braces around the lines that need it end the lock there, and mark a paragraph.
- A class that declares its data between its functions. A reader must search the whole class for its state.
- A variable declared forty lines before its first use. Declare it where it is first used, in the smallest scope.
