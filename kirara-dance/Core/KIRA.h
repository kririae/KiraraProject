#pragma once

#include <kira/Anyhow.h>
#include <kira/Assertions.h>
#include <kira/Compiler.h>
#include <kira/Logger.h>

namespace krd {
// NOLINTBEGIN
/// Log a message at the trace level.
inline kira::LoggerCustomizationPoint<"krd", spdlog::level::level_enum::trace> LogTrace;

/// Log a message at the debug level.
inline kira::LoggerCustomizationPoint<"krd", spdlog::level::level_enum::debug> LogDebug;

/// Log a message at the info level.
inline kira::LoggerCustomizationPoint<"krd", spdlog::level::level_enum::info> LogInfo;

/// Log a message at the warn level.
inline kira::LoggerCustomizationPoint<"krd", spdlog::level::level_enum::warn> LogWarn;

/// Log a message at the error level.
inline kira::LoggerCustomizationPoint<"krd", spdlog::level::level_enum::err> LogError;
// NOLINTEND

#if !defined(NDEBUG)
/// Asserts that the given condition is true.
#define KRD_ASSERT(cond, ...) KIRA_FORCE_ASSERT(cond, __VA_ARGS__)
#else
/// Asserts that the given condition is true.
#define KRD_ASSERT(cond, ...)
#endif
} // namespace krd
