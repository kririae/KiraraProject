# Kirara Project Design Log

- Kirara -> KRR
- Kirara Infrastructure -> KIRA
- Designed to be GCC/Clang/MSVC compatible.
- ALL components in `kira` are always enabled (for python-interface). This only
  complicates the developers' setup (which, is also simplified with CPM so no
  prob)
- CMake files should be self-contained, i.e., should not depend on parents'
  include.
- Relaxed naming conventions
- `SmallVector` adopted from LLVM
- <del>Thread-safe logger without DCL (use NTTP with static local variable
  initiaization), highly simple and efficient!</del> For integration with
  spdlog, use a `std::mutex` instead, anyway logger is not the bottle-neck.
- camelBack variable naming; CamelCase for type naming, functions are
  recommended to be CamelCase. Strictly following the convention is impossible
  and these are designed to be convenient and short.
- Inheritance chain trick for `Vecteur`(a nice name!), A op B boosts to
  `std::common_type`.
- Hybrid testing framework with `ut2` and `gtest`
- SSA-like operations on Vecteur
- Operations with type prompted in Vecteur, i.e., float op int -> float.
- PCH not introduced for now, for potential cxx module support.
- TODO: more Vecteur initialization schemes.
- TODO: add `mimalloc` and `backward-cpp`.
- Between `Scene`, observer pattern is used to break dependencies and avoid
  complex dependencies.
- Constructor should not throw. I don't have other approaches..
- Right-handed global coordinate system.

- Scene Object is linear, while Scene Node is tree-structured
- Traversable forms a tree and lifetime forms a graph
- No loop is allowed in both, even implicit loop implemented with raw pointer