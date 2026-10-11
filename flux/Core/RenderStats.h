#pragma once

#include <chrono>
#include <cstdint>

namespace flux {
/// \brief Timing result for one render call.
struct RenderStats {
    /// Number of camera paths that the call ran.
    std::uint64_t paths{};

    /// Backend execution time.
    std::chrono::nanoseconds elapsed{};

    [[nodiscard]] double getPathsPerSecond() const noexcept {
        if (elapsed == std::chrono::nanoseconds::zero())
            return 0.0;
        using Seconds = std::chrono::duration<double, std::chrono::seconds::period>;
        auto const seconds = std::chrono::duration_cast<Seconds>(elapsed);
        return static_cast<double>(paths) / seconds.count();
    }
};
} // namespace flux
