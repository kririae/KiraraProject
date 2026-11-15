#pragma once

#include <span>

namespace flux {
/// \brief Returns the OptiX IR of the megakernel that flux builds.
[[nodiscard]] std::span<char const> getOptixMegaKernelIr() noexcept;
} // namespace flux
