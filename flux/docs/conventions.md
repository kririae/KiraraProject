# Conventions

How FLux writes C++, beyond what `.clang-format` and `.clang-tidy` check.

- A doc comment uses `///` and opens with `\brief`. A blank `///` line separates its ideas. A precondition is `\pre`, and an error is `\throw`.
- A checked misuse throws `std::invalid_argument`, and a lookup of an absent ID or key throws `std::out_of_range`, as `std::map::at` does. A request beyond a limit of FLux's own, such as the indices that a `Context` gives one kind of object, throws `std::length_error`.
- New code initializes variables and members with braces, such as `bool failed{false};`.
- A resource owner moves as `DeviceBuffer` does. A constructor that acquires several resources starts from an empty state, as `OptixDeviceSession` does.
- Look a name up in the C++ standard library, LLVM, CUDA, and USD.
- A function name is in lower camel case, such as `getStream`, and a private data member ends with `_`, such as `handle_`.
- An accessor returns a property of its object, or an element that a key finds, and is named `getX`, such as `getContextId` or `getObject`. A predicate reads as an assertion, such as `isVisible`, `hasChannel`, or `usesShaderReorder`.
- Code that meets another library may follow its style, as [naming](../../.agents/skills/naming/SKILL.md) says. hdFlux names the functions of a class derived from a Hydra class in upper camel case, such as `Clear`, as USD and hdEmbree do. An OptiX program starts with the prefix that OptiX requires, such as `__raygen__`.
- A mimic of a standard class keeps its names, case included, as [naming](../../.agents/skills/naming/SKILL.md) says, such as `size` and `data` of a buffer.
- A test name is a sentence in the present tense that states the behavior it checks, such as `EmptyRangeDoesNotLaunch`.
- A value that fully describes what to build is a spec, named `XSpec`. Equal specs build the same result. A spec holds values only, never a pointer to a live object, since a new object at the same address would compare equal.
- Give a new type that only wraps CUDA the prefix `Cuda`, and a new type of the OptiX backend the prefix `Optix`.
- `XImpl.h` holds the inline code that the render kernels use, such as the functions of `X::Impl`. `XPImpl.h` defines the private implementation of `X`, so that a second class can reach it without accessors.
