#include "kira/Logger.h"

#include <spdlog/cfg/env.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "kira/SmallVector.h"

#if _WIN32
#include <Windows.h>
#endif

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

namespace {
std::string safe_getenv(char const *name) {
#ifdef _WIN32
    DWORD bufferSize = 0;
    std::vector<char> buffer;

    bufferSize = GetEnvironmentVariableA(name, nullptr, 0);
    if (bufferSize == 0) {
        if (GetLastError() == ERROR_ENVVAR_NOT_FOUND)
            return {};
        throw std::runtime_error("Failed to get environment variable size");
    }

    buffer.resize(bufferSize);
    DWORD result = GetEnvironmentVariableA(name, buffer.data(), bufferSize);
    if (result == 0 || result >= bufferSize)
        throw std::runtime_error("Failed to get environment variable value");

    return {buffer.data(), result};
#else
    char const *value = std::getenv(name);
    if (value == nullptr)
        return {};
    return std::string(value);
#endif
}
} // namespace

std::shared_ptr<spdlog::logger> LoggerBuilder::init() const {
    auto &sinkManager = detail::SinkManager::GetInstance();
    SmallVector<spdlog::sink_ptr> sinks;

    if (console)
        sinks.push_back(sinkManager.CreateConsoleSink());
    if (path)
        sinks.push_back(sinkManager.CreateFileSink(path.value()));
    if (not level) {
        if (auto const envVal = safe_getenv("KRR_LOG_LEVEL"); !envVal.empty())
            spdlog::cfg::helpers::load_levels(envVal);
    }

    auto logger = std::make_shared<spdlog::logger>(std::string{name}, sinks.begin(), sinks.end());

    try {
        spdlog::initialize_logger(logger);
#ifdef NDEBUG
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
#else
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] [%s:%#] %v");
#endif
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
