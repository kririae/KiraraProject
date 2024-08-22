# Vecteur

We do not recommend using this library for whenever a algebric vector is
referenced. This library intends to provide syntax sugar instead of a complete
high-performance vector library. This do also leaves the interface to build a
JIT-based library like [drjit](https://github.com/mitsuba-renderer/drjit).

This library is talored to be used in offline rendering applications, where
complex linear algebra operations are rarely needed.
