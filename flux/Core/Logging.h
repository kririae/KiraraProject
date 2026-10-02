#pragma once

#include "kira/Logger.h"

namespace flux {
// NOLINTBEGIN
/// \brief Logs detailed execution data.
inline kira::LoggerCustomizationPoint<"flux", spdlog::level::level_enum::trace> LogTrace;

/// \brief Logs information useful while debugging the renderer.
inline kira::LoggerCustomizationPoint<"flux", spdlog::level::level_enum::debug> LogDebug;

/// \brief Logs renderer configuration, progress, and results.
inline kira::LoggerCustomizationPoint<"flux", spdlog::level::level_enum::info> LogInfo;

/// \brief Reports a recoverable problem and any selected fallback.
inline kira::LoggerCustomizationPoint<"flux", spdlog::level::level_enum::warn> LogWarn;

/// \brief Reports a problem that prevents the current operation from completing.
inline kira::LoggerCustomizationPoint<"flux", spdlog::level::level_enum::err> LogError;
// NOLINTEND
} // namespace flux
