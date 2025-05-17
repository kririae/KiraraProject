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

// zpp_bits overloads
namespace linalg {
// NOTE(krr): introduce the operators into ::linalg s.t. ADL can find them
using ostream_overloads::operator<<;

/// \brief A specialization of the zpp::bits::as_bytes function for vector types.
///
/// Looked up through ADL
template <class T, int M> void serialize(auto &ar, ::linalg::vec<T, M> &value) {
    for (int i = 0; i < M; ++i)
        ar(value[i]);
}

/// \brief A specialization of the zpp::bits::as_bytes function for matrix types.
///
/// Looked up through ADL
template <class T, int M, int N> auto serialize(auto &ar, ::linalg::mat<T, M, N> &value) {
    for (int i = 0; i < M; ++i)
        ar(value[i]);
}
} // namespace linalg

namespace fmt {
template <typename T, int N> struct fmt::formatter<linalg::vec<T, N>> : fmt::ostream_formatter {};
template <typename T, int M, int N>
struct fmt::formatter<linalg::mat<T, M, N>> : fmt::ostream_formatter {};
} // namespace fmt
