#pragma once

#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/renderThread.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Core/RenderStats.h"
#include "flux/Hydra/RenderSpec.h"
#include "flux/Hydra/SampleSchedule.h"
#include "flux/Optix/OptixAccumulationBuffer.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Film.h"

class HdFLuxRenderBuffer;
class HdFLuxRenderParam;

/// \brief Renders the image of a spec into buffers, one spec at a time.
///
/// The image is what the samples converge to, and depends only on the scene of the last sync and
/// on the spec. An output is converged once it holds an accumulation of the image at the target.
///
/// The render thread accumulates samples while an output is not converged, until the param fails.
class HdFLuxRenderer final {
public:
    /// \brief A buffer that receives one channel of the image.
    struct Output {
        HdFLuxRenderBuffer *buffer;
        flux::FilmChannels channel;
        pxr::VtValue clearValue;

        bool operator==(Output const &) const = default;
    };

public:
    // Called on Hydra threads.

    /// \pre The param and the render thread outlive the renderer.
    HdFLuxRenderer(HdFLuxRenderParam &param, pxr::HdRenderThread &renderThread);

    /// \brief Makes the render thread render the image of a spec into outputs, up to a schedule.
    ///
    /// \pre A buffer of the outputs is finalized before it is destroyed.
    /// \pre Every output with a channel has the resolution of the spec, and the spec has its
    /// channel.
    /// \throw kira::Anyhow If CUDA or OptiX fails.
    void StartRender(
        HdFLuxRenderSpec const &spec, std::span<Output const> outputs,
        HdFLuxSampleSchedule const &schedule
    );

    [[nodiscard]] bool IsConverged() const;

    [[nodiscard]] double GetPercentDone() const noexcept { return percentDone_; }

    [[nodiscard]] double GetTotalClockTime() const noexcept { return totalClockTime_; }

public:
    // Called inside the render callback of the render thread.

    /// A failure is reported to the param.
    void Render() noexcept;

private:
    /// \brief The samples of one image accumulated so far.
    struct Accumulation {
        Accumulation(
            flux::OptixDeviceSession const &session, flux::FilmSpec const &spec,
            HdFLuxRenderSpec::View const &view
        );

        flux::OptixAccumulationBuffer accumulationBuffer;
        flux::Film film;
        flux::Ref<flux::Camera> camera;
        std::chrono::steady_clock::time_point start;
        flux::RenderStats stats;
    };

    [[nodiscard]] bool isAtTarget() const;
    void launch();
    void writeOutputs() const;
    void setConverged(bool converged) const;
    void publishProgress();

private:
    HdFLuxRenderParam &param_;
    pxr::HdRenderThread &renderThread_;

    // Written only while the render thread is stopped.
    std::uint64_t syncedVersion_;
    HdFLuxRenderSpec spec_;
    HdFLuxSampleSchedule schedule_;
    std::vector<Output> outputs_;

    // Replaced only while the render thread is stopped, and raised by the render thread. Null if
    // the spec renders nothing.
    std::unique_ptr<Accumulation> accumulation_;

    // Written by the render thread.
    std::atomic<double> percentDone_{0.0};
    std::atomic<double> totalClockTime_{0.0};
};
