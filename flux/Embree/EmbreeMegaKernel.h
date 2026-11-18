#pragma once

#include <cstddef>

namespace flux {
struct EmbreeLaunchParams;

namespace embree {
/// \brief Runs one megakernel work item.
///
/// The caller may invoke this function concurrently for distinct indices.
/// \pre \p params is bound to the current thread.
/// \pre \p linearIndex is less than `params.film.width * params.film.height`.
void runMegaKernel(EmbreeLaunchParams const &params, std::size_t linearIndex) noexcept;
} // namespace embree
} // namespace flux
