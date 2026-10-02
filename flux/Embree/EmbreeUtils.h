#pragma once

#include <embree4/rtcore.h>

#include <source_location>

#include "kira/Anyhow.h"

namespace flux {
/// \brief Throws when the Embree device has a pending error.
///
/// Reading the error clears it. Call this immediately after API operations
/// that do not return an error code themselves.
inline void
embreeCheck(RTCDevice device, std::source_location location = std::source_location::current()) {
    auto const error = rtcGetDeviceError(device);
    if (error == RTC_ERROR_NONE)
        return;

    auto const *message = rtcGetDeviceLastErrorMessage(device);
    throw kira::Anyhow(
        "Embree API error {} ({}): \"{}\" at {}:{}", static_cast<int>(error),
        rtcGetErrorString(error), message ? message : "", location.file_name(), location.line()
    );
}
} // namespace flux
