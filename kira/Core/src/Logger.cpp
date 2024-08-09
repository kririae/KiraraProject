#include <spdlog/cfg/env.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "kira/Logger.h"
#include "kira/SmallVector.h"

namespace kira {
namespace detail {
spdlog::sink_ptr SinkManager::CreateConsoleSink() {
    if (!consoleSink)
        consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    return consoleSink;
}

spdlog::sink_ptr SinkManager::CreateFileSink(std::filesystem::path const &path) {
    auto it = fileSinks.find(path);
    if (it == fileSinks.end()) {
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
        fileSinks[path] = sink;
        return sink;
    }
    return it->second;
}

bool SinkManager::DropConsoleSink() noexcept {
    if (consoleSink) {
        consoleSink.reset();
        return true;
    }
    return false;
}

bool SinkManager::DropFileSink(std::filesystem::path const &path) noexcept {
    auto it = fileSinks.find(path);
    if (it != fileSinks.end()) {
        fileSinks.erase(it);
        return true;
    }
    return false;
}

bool SinkManager::DropAllSinks() noexcept {
    auto const changed = consoleSink.get() != nullptr || !fileSinks.empty();
    consoleSink.reset();
    fileSinks.clear();
    return changed;
}
} // namespace detail

std::shared_ptr<spdlog::logger> LoggerBuilder::init() {
    auto &sinkManager = detail::SinkManager::GetInstance();
    SmallVector<spdlog::sink_ptr> sinks;

    if (console)
        sinks.push_back(sinkManager.CreateConsoleSink());
    if (path)
        sinks.push_back(sinkManager.CreateFileSink(path.value()));
    if (not level) {
        auto const *envVal = std::getenv("KRR_LOG_LEVEL");
        if (envVal != nullptr)
            spdlog::cfg::helpers::load_levels(envVal);
    }

    auto logger = std::make_shared<spdlog::logger>(std::string{name}, sinks.begin(), sinks.end());

    try {
        spdlog::initialize_logger(logger);
    } catch (std::exception const &e) {
        auto const str = fmt::format("kira: Failed to initialize logger: {}\n", e.what());
        fmt::print(stderr, "{:s}", str);
        std::fflush(stderr);
        throw std::runtime_error(str);
    }

    // Override the environment variable if the level is set.
    if (level)
        logger->set_level(level.value());
    return logger;
}
} // namespace kira
