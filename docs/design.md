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
- `SmallVector` adapted from LLVM
- Thread-safe logger without DCL (use NTTP with static local variable
  initiaization), highly simple and efficient!
- camelBack variable naming; CamelCase for type naming, functions are
  recommended to be CamelCase. Strictly following the convention is impossible
  and these are designed to be convenient and short.
- If a feature is not immediately necessary, don't add to codebase.
- Inheritance chain trick for `Vecteur`(a nice name!), A op B boosts to
  `std::common_type`.