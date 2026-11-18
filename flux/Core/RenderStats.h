#pragma once

#include <chrono>
#include <cstdint>

namespace flux {
struct RenderStats {
    std::uint64_t paths{};

    /// Backend execution time.
    std::chrono::nanoseconds elapsed{};

    RenderStats &operator+=(RenderStats const &other) noexcept {
        paths += other.paths;
        elapsed += other.elapsed;
        return *this;
    }

    [[nodiscard]] double getPathsPerSecond() const noexcept {
        if (elapsed == std::chrono::nanoseconds::zero())
            return 0.0;
        using Seconds = std::chrono::duration<double, std::chrono::seconds::period>;
        auto const seconds = std::chrono::duration_cast<Seconds>(elapsed);
        return static_cast<double>(paths) / seconds.count();
    }

    void log() const;
};
} // namespace flux
