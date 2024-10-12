#pragma once

#include <fmt/ostream.h>

#include "Core/detail/Linalg.h"

// NOTE(krr): Use linalg.h for now because it is easier to be modified and is more lightweight.
// I know it is a bit weird to introduce multiple math libraries in a single project for now, but I
// don't have energy to implement the math library myself.
//
//
// A few things to notice here:
// - linalg uses col-major matrix while Slang expects row-major.
namespace krd {
using namespace linalg;
using namespace linalg::aliases;
} // namespace krd

// fmt overloads
using namespace linalg::ostream_overloads;
template <typename T, int N> struct fmt::formatter<linalg::vec<T, N>> : fmt::ostream_formatter {};
template <typename T, int M, int N>
struct fmt::formatter<linalg::mat<T, M, N>> : fmt::ostream_formatter {};
