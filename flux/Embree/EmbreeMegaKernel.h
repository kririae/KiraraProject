#pragma once

#include <cstddef>

namespace flux {
struct EmbreeLaunchParams;

namespace embree {
/// \brief Runs one megakernel work item and its sample batch.
///
/// The caller may invoke this function concurrently for distinct linear indices.
void runMegaKernel(EmbreeLaunchParams const &params, std::size_t linearIndex) noexcept;
} // namespace embree
} // namespace flux
