#pragma once

#include <slang-gfx.h>
#include <slang.h>

#include <magic_enum.hpp>
#include <source_location>
#include <string_view>

#include "Core/KIRA.h"

namespace krd {
/// Check the result of a Slang API call.
///
/// \param result The result from a Slang API call.
/// \param loc The source location where the check is called, defaults to the current location.
/// \throws kira::Anyhow Throws this exception if \c bShouldThrow is true.
template <bool bShouldThrow = true>
auto slangCheck(
    SlangResult result, std::source_location loc = std::source_location::current()
) noexcept(not bShouldThrow) -> void {
    constexpr auto makeSlangResultString = [](SlangResult result) -> std::string_view {
        switch (result) {
        case SLANG_OK: return "SLANG_OK";
        case SLANG_FAIL: return "SLANG_FAIL";
        case SLANG_E_NOT_IMPLEMENTED: return "SLANG_E_NOT_IMPLEMENTED";
        case SLANG_E_NO_INTERFACE: return "SLANG_E_NO_INTERFACE";
        case SLANG_E_ABORT: return "SLANG_E_ABORT";
        case SLANG_E_INVALID_HANDLE: return "SLANG_E_INVALID_HANDLE";
        case SLANG_E_INVALID_ARG: return "SLANG_E_INVALID_ARG";
        case SLANG_E_OUT_OF_MEMORY: return "SLANG_E_OUT_OF_MEMORY";
        case SLANG_E_BUFFER_TOO_SMALL: return "SLANG_E_BUFFER_TOO_SMALL";
        case SLANG_E_UNINITIALIZED: return "SLANG_E_UNINITIALIZED";
        case SLANG_E_PENDING: return "SLANG_E_PENDING";
        case SLANG_E_CANNOT_OPEN: return "SLANG_E_CANNOT_OPEN";
        case SLANG_E_NOT_FOUND: return "SLANG_E_NOT_FOUND";
        case SLANG_E_INTERNAL_FAIL: return "SLANG_E_INTERNAL_FAIL";
        case SLANG_E_NOT_AVAILABLE: return "SLANG_E_NOT_AVAILABLE";
        case SLANG_E_TIME_OUT: return "SLANG_E_TIME_OUT";
        default: return "SLANG_UNKNOWN";
        }
    };

    if (KIRA_UNLIKELY(SLANG_FAILED(result))) {
        auto const str = fmt::format(
            "slangCheck(): Slang API call error {:d} ({:s}) at {:s}:{:d}", result,
            makeSlangResultString(result), loc.file_name(), loc.line()
        );

        if constexpr (bShouldThrow)
            throw kira::Anyhow("{}", str);
        else
            LogError("{}", str);
    }
}

/// Trim the Slang callback log of newline characters.
///
/// \param str The input string to be trimmed.
/// \return A new string with leading and trailing newlines removed.
inline auto slangTrim(std::string const &str) -> std::string {
    if (str.empty())
        return {};
    auto start = str.find_first_not_of('\n');
    auto end = str.find_last_not_of('\n');
    return str.substr(start, end - start + 1);
};

/// Log the diagnostic message from Slang.
inline auto slangDiagnostic(slang::IBlob *diagnostics) -> void {
    if (KIRA_UNLIKELY(!diagnostics))
        return;
    LogWarn(
        "slangDiagnostic(): {:s}",
        slangTrim(std::string{
            static_cast<char const *>(diagnostics->getBufferPointer()), diagnostics->getBufferSize()
        })
    );
}

/// The callback object for handling debug messages from GFX.
struct GfxDebugCallback final : gfx::IDebugCallback {
    virtual ~GfxDebugCallback() = default;

    void SLANG_NO_THROW handleMessage(
        gfx::DebugMessageType type, gfx::DebugMessageSource source, char const *message
    ) override {
        (void)(source); // `source` is unused
        auto const trimedMessage = slangTrim(message);
        auto const sourceName = magic_enum::enum_name(source);
        switch (type) {
        case gfx::DebugMessageType::Info:
            krd::LogInfo("[gfx::{:s}] {:s}", sourceName, trimedMessage);
            break;
        case gfx::DebugMessageType::Warning:
            krd::LogWarn("[gfx::{:s}] {:s}", sourceName, trimedMessage);
            break;
        case gfx::DebugMessageType::Error:
            krd::LogError("[gfx::{:s}] {:s}", sourceName, trimedMessage);
            break;
        }
    }
};

/// The global callback object used by \c gfxSetDebugCallback.
inline GfxDebugCallback gfxDebugCallback{};
} // namespace krd
