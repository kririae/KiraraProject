#include <spdlog/cfg/env.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "kira/Logger.h"

namespace kira::detail {
SmallVector<spdlog::sink_ptr> const &
CreateSinks(bool console, std::optional<std::filesystem::path> const &path) {
  static SmallVector<spdlog::sink_ptr> sinks = [&] {
    SmallVector<spdlog::sink_ptr> sinks;
    if (KIRA_LIKELY(console))
      sinks.emplace_back(
          std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    if (path.has_value())
      sinks.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(
          path->string(), true));
    return sinks;
  }();

  return sinks;
}

std::shared_ptr<spdlog::logger>
CreateLogger(std::string_view name, bool console,
             std::optional<std::filesystem::path> const &path) {
  auto const &sinks = CreateSinks(console, path);

  auto const *envVal = std::getenv("KRR_LOG_LEVEL");
  if (envVal != nullptr)
    spdlog::cfg::helpers::load_levels(envVal);
  auto logger = std::make_shared<spdlog::logger>(std::string{name},
                                                 sinks.begin(), sinks.end());
  spdlog::initialize_logger(logger);

  return logger;
}
} // namespace kira::detail
