#pragma once

#include <pxr/base/vt/value.h>

#include <cstdint>
#include <memory>
#include <span>

#include "flux/Core/Object.h"
#include "flux/Hydra/RenderSpec.h"
#include "flux/Hydra/SampleSchedule.h"
#include "flux/Optix/OptixAccumulationBuffer.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Film.h"

class HdFluxRenderBuffer;
class HdFluxRenderParam;

/// \brief Renders the image of a spec into buffers, one spec at a time.
class HdFluxRenderer final {
public:
    /// \brief A buffer that receives one channel of the image.
    struct Output {
        HdFluxRenderBuffer *buffer;
        flux::FilmChannels channel;
        pxr::VtValue clearValue;
    };

    /// \pre \p param outlives the renderer.
    explicit HdFluxRenderer(HdFluxRenderParam &param);

    /// \brief Sets the samples to reach and the samples per launch.
    ///
    /// The image is not restarted.
    void SetSchedule(HdFluxSampleSchedule const &schedule) noexcept { schedule_ = schedule; }

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
    void Render(HdFluxRenderSpec const &spec, std::span<Output const> outputs);

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

    HdFluxRenderParam &param_;
    HdFluxSampleSchedule schedule_;

    /// The spec of the last render.
    HdFluxRenderSpec spec_;
    std::uint64_t contextVersion_;

    /// The image of \c spec_, or null if \c spec_ renders nothing.
    std::unique_ptr<Image> image_;

    /// The camera of \c spec_, converted when the spec changes.
    flux::Ref<flux::Camera> camera_;
};
