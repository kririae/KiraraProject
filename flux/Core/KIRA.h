#pragma once

#include "kira/Anyhow.h"
#include "kira/Logger.h"

namespace flux {
// NOLINTBEGIN
/// \brief Logs a message at the trace level.
inline kira::LoggerCustomizationPoint<"flux", spdlog::level::level_enum::trace> LogTrace;

/// \brief Logs a message at the debug level.
inline kira::LoggerCustomizationPoint<"flux", spdlog::level::level_enum::debug> LogDebug;

/// \brief Logs a message at the info level.
inline kira::LoggerCustomizationPoint<"flux", spdlog::level::level_enum::info> LogInfo;

/// \brief Logs a message at the warning level.
inline kira::LoggerCustomizationPoint<"flux", spdlog::level::level_enum::warn> LogWarn;

/// \brief Logs a message at the error level.
inline kira::LoggerCustomizationPoint<"flux", spdlog::level::level_enum::err> LogError;
// NOLINTEND
} // namespace flux
