#include "flux/Core/RenderStats.h"

#include "flux/Core/Logging.h"

namespace flux {
void RenderStats::log() const {
    using Milliseconds = std::chrono::duration<double, std::chrono::milliseconds::period>;

    LogInfo(
        "render statistics\n"
        "\tcamera paths               : {:>8}\n"
        "\trender time (ms)           : {:>8.1f}\n"
        "\tthroughput (Mpaths/s)      : {:>8.1f}",
        paths, std::chrono::duration_cast<Milliseconds>(elapsed).count(),
        getPathsPerSecond() / 1.0e6
    );
}
} // namespace flux
