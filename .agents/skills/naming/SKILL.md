---
name: naming
description: Use when you name an entity, such as a type, a function, a variable, a file, or a test.
---

# Naming

A name is read more often than any other part of code, and a reader trusts it before reading the code behind it. So a bad name is the first cause of unreadable code. It misleads at every use, and unlike a wrong value, it compiles and passes every test. "Poorly-chosen names can mislead the reader and cause bugs." ([LLVM Coding Standards](https://llvm.org/docs/CodingStandards.html#name-types-functions-variables-and-enumerators-properly))

Give a name the effort that a proposal for a standard library gives it. Such a name is read by every user for decades and cannot change, so the proposal defines it, compares it with the prior art, and argues for it.

- A name is the shortest true definition of its entity, judged where it is used, not where it is declared.
- Use the word that the precedents and the [references](../design/SKILL.md) use for the same concept, in its established meaning. Give each concept one word across the place.
- When no name fits, go back to [design](../design/SKILL.md).

Each kind of entity takes one form. The C++ standard library, for example, names them so:

| Entity | Form | Examples |
|---|---|---|
| A type, a variable, or a constant | A noun for what it is, or for its role | `vector`, `mutex`, `string_view` |
| A function with an effect | An imperative verb | `sort`, `insert`, `reserve` |
| A function that returns a property of its object | A noun for the property | `size`, `capacity`, `front` |
| A function that computes a value from its arguments | A verb for the computation, or the name that its field gives it | `find`, `count`, `max`, `sqrt` |
| A predicate | An assertion about its object | `is_open`, `has_value`, `contains` |
| A conversion or a factory | `to`, `as`, or `make` | `to_string`, `as_const`, `make_unique` |
| A variant of an operation | A word that names the difference | `try_lock`, `find_if`, `copy_n`, `wait_for` |
| An object that holds a state for a scope | `guard`, or `scoped` | `lock_guard`, `scoped_lock` |
| Two operations that form a pair | A standard pair | `begin` and `end`, `push` and `pop`, `acquire` and `release`, `lock` and `unlock` |

The words carry over to any house style, and the house style decides the case and any prefix. In a house that writes camel case and names its accessors with `get`, `is_open` becomes `isOpen`, and `name` becomes `getName`. A house style is best effort, since the libraries that code uses already differ in style. Code that meets another library may follow its style there, as a class that derives from a class of a framework may name its functions as the framework does.

A class that does the job of a class of the standard library mimics it, and its comment names that class. It keeps the names of that class, case included, in their standard meanings, as `llvm::SmallVector` keeps `size`, `empty`, and `push_back`. Generic code, such as `std::back_inserter`, calls them by name.

## Steps

1. Define the entity in one sentence, in the words of its caller: what it is, or what it does or returns. If the sentence needs "and", go back to design.
2. List candidate words. For each, find where the precedents and the references use it, and what it means there, as [survey](../survey/SKILL.md) says. Drop each word whose established meaning differs from your definition.
3. Read each candidate at a use site. Drop the words that the scope and the types already say, and add what the use site lacks. A wider scope needs a longer name.
4. Check:
   1. For each new or renamed public name, write a naming entry in the design or the report: its definition, its candidates with their sources, the choice and why, and one use site. A name with no source is a fault, unless no precedent exists.
   2. Give an agent that has not read the code only the declarations of the new public names, without comments, and have it define each one. Rename each name that it defines differently from you.
   3. In your reasoning, list each concept that the change names, and the term that the place already uses for it. Rename where the change brings a second term for a concept, or a term with another meaning.

Rename a bad name that you find in code you change, in a commit of its own.

## Bad forms

- `class Render`. A type is a noun: `Renderer`.
- `renderer.frame()` for a function that renders a frame. An action is a verb.
- `getRender()` for a function that renders an image. `get` reads what exists, and the action is to render.
- `auto load = readImage(path);` A variable holds state, so it is a noun: `image`.
- `bool check()`. It does not say which result means what. A predicate reads as an assertion: `isValid()`.
- `bool visibility`. It names a property, not an assertion about it: `visible`.
- `isNotEmpty`, or `disableCache = false`. A negation in a name doubles in every test of it.
- `open()` with `destroy()`. Two operations that form a pair take a standard pair: `open` and `close`.
- `mesh` for a list of meshes. A collection is plural.

## Bad meanings

- `std::remove`, which moves the kept elements forward and leaves the size of the container unchanged. A word against its established meaning trips every reader, even in a standard library.
- `find` that throws when the key is absent. Elsewhere, `find` reports an absent key in its result, and `at` throws.
- `clear()` on a class that mimics `std::vector`, where it frees the memory. A mimic keeps the standard meanings, and `std::vector::clear` keeps the capacity.
- `takeChunk` for a function that hands out the next chunk of a loop without waiting. A queue's `take` waits for an item, as `BlockingQueue.take` in Java does. Schedulers claim or fetch work, or return the next item, as `GOMP_loop_dynamic_next` does.
- `meshIndex` for an ID that never indexes an array. An index is a position.
- `size` for bytes in one class, and for elements in another. One word has two meanings.
- `clone()` that shares the data. A clone is independent.

## Bad scopes

- `Parser::ParserState`. The scope already says parser.
- `i` as a member, or `n` as a global. A wide scope needs a long name.
- `theTextureLoadedLast` in a three-line loop. A narrow scope needs a short name.
- `meshVector`, `pBuffer`, or `printInt`. The name repeats the type, and goes wrong when the type changes.
- `setTimeout(int)`. A weak type needs its role or its unit, such as `std::chrono::milliseconds`.

## Bad origins

- `cachedSize` for the size that a function returns. It names the strategy that produced the value.
- `FontKey` for a value that describes a font. A key names how a cache looks it up, not what the value is.
- `binarySearchIndex()` for a lookup. It names how, and the caller needs what: `find`.
- `Renderer2`, or `NewRenderer` beside the class it replaces. A name from the history means nothing to a reader who sees only the result.
- `memberScope` for a member that sets a context and restores it on exit. That is a guard, as in `std::lock_guard`.

## Bad vague names

- `data`, `info`, or `tmp` for a member. Each fits every entity, so it says nothing about this one.
- `result1` and `result2`. A number tells the two apart, and says nothing about either.
- `process()`, `handle()`, or `doWork()`. The verb names no operation.
- `class RenderHelper`, or `class MeshUtils`. A word that fits anything hides a class that owns several things.
- `calcTotHits`, or `rdr`. A search does not explain the abbreviation.
- `splunk` beside `splonk`. The two are easily misread.
- `Test1`, or `Works`. A test name states the behavior that it checks.
