#pragma once

#include <pxr/base/vt/value.h>

#include <cstdint>
#include <memory>
#include <span>

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
class HdFLuxRenderer final {
public:
    /// \brief A buffer that receives one channel of the image.
    struct Output {
        HdFLuxRenderBuffer *buffer;
        flux::FilmChannels channel;
        pxr::VtValue clearValue;
    };

    /// \pre \p param outlives the renderer.
    explicit HdFLuxRenderer(HdFLuxRenderParam &param);

    /// \brief Sets the samples to reach and the samples per launch.
    ///
    /// The image is not restarted.
    void SetSchedule(HdFLuxSampleSchedule const &schedule) noexcept { schedule_ = schedule; }

    /// \brief Adds samples to the image of \p spec and writes the image into \p outputs.
    ///
    /// A change of \p spec or of the scene starts the image over. An output with a channel holds
    /// the image once it has a sample. Any other output holds its clear value, if it has one. Every
    /// buffer of \p outputs ends with the converged state of the image.
    ///
    /// \pre The buffers of \p outputs are alive.
    /// \pre Every output with a channel has the resolution of \p spec, and \p spec has its channel.
    /// \throw std::length_error If the image of \p spec has more than 2^32 - 1 pixels.
    /// \throw kira::Anyhow If CUDA or OptiX fails.
    void Render(HdFLuxRenderSpec const &spec, std::span<Output const> outputs);

    /// \brief Returns whether the image has its samples, nothing is rendered, or the param failed.
    [[nodiscard]] bool IsConverged() const;

private:
    /// The film and the accumulation buffer that render one spec.
    struct Image {
        Image(flux::OptixHandler &handler, flux::FilmSpec const &spec);

        flux::Film film;
        flux::OptixAccumulationBuffer accumulation;
    };

    void setConverged(std::span<Output const> outputs) const;

    HdFLuxRenderParam &param_;
    HdFLuxSampleSchedule schedule_;

    /// The spec of the last render.
    HdFLuxRenderSpec spec_;
    std::uint64_t contextVersion_;

    /// The image of \c spec_, or null if \c spec_ renders nothing.
    std::unique_ptr<Image> image_;

    /// The sum over the launches that gave the image its samples.
    flux::RenderStats renderStats_;

    /// The camera of \c spec_, converted when the spec changes.
    flux::Ref<flux::Camera> camera_;
};
