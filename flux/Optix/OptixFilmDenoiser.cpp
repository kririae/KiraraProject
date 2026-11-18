#include "flux/Optix/OptixFilmDenoiser.h"

#include <optix_stubs.h>

#include <cstddef>
#include <new>
#include <optional>
#include <stdexcept>

#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/OptixDeviceSession.h"
#include "flux/Optix/OptixUtils.h"

namespace flux {
static_assert(sizeof(Vec3f) == 3 * sizeof(float), "OPTIX_PIXEL_FORMAT_FLOAT3 reads packed Vec3f");

struct OptixFilmDenoiser::pImpl final {
    pImpl(OptixDeviceSession const &session, Vec2u resolution);
    ~pImpl();

    [[nodiscard]] OptixImage2D toOptixImage(Vec3f const *channel) const noexcept;

    /// Outlives the denoiser. Its stream orders the work and the storage.
    OptixDeviceSession const &session;
    Vec2u resolution;

    /// Holds the primary context of the session current while the members after it are built or
    /// destroyed. It is empty unless a pImpl constructor or destructor is running.
    std::optional<CudaContextGuard> cudaContextGuard;

    /// Null, or an AOV denoiser with albedo and normal guides, set up for the resolution.
    OptixDenoiser denoiser{};
    DeviceBuffer<std::byte> state;
    DeviceBuffer<std::byte> scratch;

private:
    struct EmptyState {};

    pImpl(EmptyState, OptixDeviceSession const &borrowedSession, Vec2u resolution) noexcept
        : session(borrowedSession), resolution(resolution), state(borrowedSession.getStream()),
          scratch(borrowedSession.getStream()) {}
};

OptixFilmDenoiser::pImpl::pImpl(OptixDeviceSession const &deviceSession, Vec2u denoiserResolution)
    : pImpl(EmptyState{}, deviceSession, denoiserResolution) {
    cudaContextGuard.emplace(session.getPrimaryContext());

    auto const options = OptixDenoiserOptions{
        .guideAlbedo = 1,
        .guideNormal = 1,
        .denoiseAlpha = OPTIX_DENOISER_ALPHA_MODE_COPY,
    };
    optixCheck(
        optixDenoiserCreate(session.get(), OPTIX_DENOISER_MODEL_KIND_AOV, &options, &denoiser)
    );

    OptixDenoiserSizes sizes{};
    optixCheck(
        optixDenoiserComputeMemoryResources(denoiser, resolution.x(), resolution.y(), &sizes)
    );
    state.resize(sizes.stateSizeInBytes);
    scratch.resize(sizes.withoutOverlapScratchSizeInBytes);
    optixCheck(optixDenoiserSetup(
        denoiser, session.getStream(), resolution.x(), resolution.y(), devicePointer(state.data()),
        state.size(), devicePointer(scratch.data()), scratch.size()
    ));

    cudaContextGuard.reset();
}

OptixFilmDenoiser::pImpl::~pImpl() {
    cudaContextGuard.emplace(session.getPrimaryContext(), std::nothrow);
    if (!denoiser)
        return;

    cudaCheck<false>(cudaStreamSynchronize(session.getStream()));
    optixCheck<false>(optixDenoiserDestroy(denoiser));
}

OptixImage2D OptixFilmDenoiser::pImpl::toOptixImage(Vec3f const *channel) const noexcept {
    return {
        .data = devicePointer(channel),
        .width = resolution.x(),
        .height = resolution.y(),
        .rowStrideInBytes = static_cast<unsigned int>(resolution.x() * sizeof(Vec3f)),
        .pixelStrideInBytes = sizeof(Vec3f),
        .format = OPTIX_PIXEL_FORMAT_FLOAT3,
    };
}

OptixFilmDenoiser::OptixFilmDenoiser(OptixDeviceSession const &session, Vec2u resolution)
    : pImpl_(std::make_unique<pImpl>(session, resolution)) {}

OptixFilmDenoiser::~OptixFilmDenoiser() = default;

Vec2u OptixFilmDenoiser::getResolution() const noexcept { return pImpl_->resolution; }

void OptixFilmDenoiser::denoise(Film::Impl const &film) {
    auto &impl = *pImpl_;
    if (!film.hasChannel<ColorChannel>() || !film.hasChannel<AlbedoChannel>() ||
        !film.hasChannel<NormalChannel>() || !film.hasChannel<DenoisedColorChannel>())
        throw std::invalid_argument(
            "OptixFilmDenoiser: film lacks color, albedo, normal, or denoised color"
        );
    if (film.width != impl.resolution.x() || film.height != impl.resolution.y())
        throw std::invalid_argument(
            "OptixFilmDenoiser: film differs in resolution from the denoiser"
        );

    OptixDenoiserGuideLayer guides{};
    guides.albedo = impl.toOptixImage(film.channels.get<AlbedoChannel>().data);
    guides.normal = impl.toOptixImage(film.channels.get<NormalChannel>().data);
    OptixDenoiserLayer layer{};
    layer.input = impl.toOptixImage(film.channels.get<ColorChannel>().data);
    layer.output = impl.toOptixImage(film.channels.get<DenoisedColorChannel>().data);
    auto const params = OptixDenoiserParams{};

    CudaContextGuard const guard(impl.session.getPrimaryContext());
    optixCheck(optixDenoiserInvoke(
        impl.denoiser, impl.session.getStream(), &params, devicePointer(impl.state.data()),
        impl.state.size(), &guides, &layer, 1, 0, 0, devicePointer(impl.scratch.data()),
        impl.scratch.size()
    ));
}
} // namespace flux
