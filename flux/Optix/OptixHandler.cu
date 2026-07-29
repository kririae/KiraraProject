#include <cuda_runtime_api.h>
#include <optix_function_table_definition.h>
#include <optix_stubs.h>

#include <cassert>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "flux/Optix/DeviceBuffer.h"
#include "flux/Optix/KernelUtils.cuh"
#include "flux/Optix/OptixContext.h"
#include "flux/Optix/OptixHandler.h"
#include "flux/Optix/OptixLaunchParams.h"
#include "flux/Optix/OptixProgram.h"
#include "flux/Optix/OptixRenderProductPool.h"
#include "flux/Optix/OptixUtils.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/Camera.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/FilmImpl.h"
#include "flux/Scene/RenderProduct.h"
#include "kira/Anyhow.h"

namespace flux {
namespace {
/// \brief Owns the CUDA execution state used by one OptiX handler.
///
/// This object precedes every dependent handler member, so its stream and
/// device context remain valid until those members have released their
/// resources.
class OptixDeviceContextHandle final : private Noncopyable {
public:
    /// \brief Creates a non-blocking stream and an OptiX context on CUDA device 0.
    OptixDeviceContextHandle() : OptixDeviceContextHandle(EmptyState{}) {
        int deviceCount = 0;
        cudaCheck(cudaGetDeviceCount(&deviceCount));
        if (deviceCount == 0)
            throw kira::Anyhow("OptixHandler: no CUDA device is available");

        // ponytail: use device 0 until the renderer exposes multi-GPU selection.
        selectDevice();
        cudaCheck(cudaStreamCreateWithFlags(&stream_, cudaStreamNonBlocking));
        optixCheck(optixInit());

        OptixDeviceContextOptions options{};
        optixCheck(optixDeviceContextCreate(nullptr, &options, &context_));
    }

    /// \brief Waits for dependent cleanup and releases the owned CUDA state.
    ~OptixDeviceContextHandle() {
        selectDevice<false>();
        if (stream_)
            cudaCheck<false>(cudaStreamSynchronize(stream_));
        if (context_)
            optixCheck<false>(optixDeviceContextDestroy(context_));
        if (stream_)
            cudaCheck<false>(cudaStreamDestroy(stream_));
    }

    /// \brief Returns the owned OptiX device context.
    [[nodiscard]] OptixDeviceContext get() const noexcept { return context_; }

    /// \brief Returns the stream used by every dependent resource.
    [[nodiscard]] cudaStream_t getStream() const noexcept { return stream_; }

    /// \brief Selects the CUDA device that owns this handle.
    template <bool ShouldThrow = true> void selectDevice() const noexcept(not ShouldThrow) {
        cudaCheck<ShouldThrow>(cudaSetDevice(deviceId));
    }

private:
    struct EmptyState {};

    // Completing this target constructor makes the destructor responsible for
    // resources acquired before the public constructor body throws.
    explicit OptixDeviceContextHandle(EmptyState) noexcept {}

    static constexpr int deviceId = 0;

    cudaStream_t stream_{};
    OptixDeviceContext context_{};
};

[[nodiscard]] Ref<Context> requireContext(Ref<Context> context) {
    if (!context)
        throw kira::Anyhow("OptixHandler: context must not be null");
    return context;
}

inline constexpr std::uint64_t maxOptixLaunchDimension = std::uint64_t{1} << 30U;

struct ScaleFilmChannels {
    Film::Impl film;
    float factor;

    KIRA_DEVICE void operator()(std::size_t index) const noexcept { film.scale(index, factor); }
};
} // namespace

struct OptixHandler::Impl final {
    Impl(Ref<Context> hostContext, std::filesystem::path const &modulePath);
    ~Impl();

    /// \brief Rebuilds the OptiX scene from the host \c Context.
    void sync();

    /// \brief Uploads \p params and launches a one-dimensional grid.
    void launch(OptixLaunchParams const &params, std::uint32_t size);

    /// \brief Returns the stream shared by this backend's device resources.
    [[nodiscard]] cudaStream_t getStream() const noexcept { return deviceContext.getStream(); }

    /// Host \c Context retained for the lifetime of the backend.
    Ref<Context> context;

    /// CUDA execution state destroyed after every dependent resource.
    OptixDeviceContextHandle deviceContext;

    OptixContext optixContext;
    OptixRenderProductPool renderProducts{getStream()};
    DeviceBuffer<OptixLaunchParams> launchParams{getStream()};
    std::uint64_t sampleOffset{};
};

OptixHandler::Impl::Impl(Ref<Context> hostContext, std::filesystem::path const &modulePath)
    : context(requireContext(std::move(hostContext))),
      optixContext(*context, deviceContext.get(), getStream(), modulePath) {
    sync();
}

OptixHandler::Impl::~Impl() {
    // Member destruction may enqueue releases, so restore their owning device
    // before C++ begins destroying them in reverse declaration order.
    deviceContext.selectDevice<false>();
}

void OptixHandler::Impl::sync() {
    deviceContext.selectDevice();

    // Clear accumulation before rebuilding the OptiX scene.
    renderProducts.resetAccumulation();
    optixContext.sync();
}

void OptixHandler::Impl::launch(OptixLaunchParams const &params, std::uint32_t size) {
    launchParams.copyFromHost({&params, 1}, getStream());
    optixContext.launch(
        getStream(), devicePointer(launchParams.data()), sizeof(OptixLaunchParams), size
    );
}

OptixHandler::OptixHandler(Ref<Context> context, std::filesystem::path const &modulePath)
    : impl_(std::make_unique<Impl>(std::move(context), modulePath)) {}

OptixHandler::~OptixHandler() = default;

void OptixHandler::sync() { impl_->sync(); }

void OptixHandler::render(RenderProduct const &product, std::uint32_t samples) {
    if (samples == 0)
        throw std::invalid_argument("OptixHandler: sample batch must be nonzero");

    // Resolve the Camera, Film, and Sampler for this launch.
    auto const &film = product.getFilm();
    auto const resolution = Vec2u{film.getWidth(), film.getHeight()};
    auto const sampler = impl_->context->getActiveSampler();
    auto const camera = product.getCamera().getImpl();

    // The module contains bound values. A changed Context spec requires sync
    // before launch.
    if (impl_->optixContext.getProgramSpec() != OptixProgram::makeSpec(*impl_->context))
        throw kira::Anyhow(
            "OptixHandler: program specialization changed; call sync before rendering"
        );

    // Launch one work item per pixel sample and keep each pixel's samples
    // consecutive.
    auto const pixelCount =
        static_cast<std::uint64_t>(film.getWidth()) * static_cast<std::uint64_t>(film.getHeight());
    if (pixelCount > maxOptixLaunchDimension || samples > maxOptixLaunchDimension / pixelCount)
        throw std::invalid_argument("OptixHandler: render batch exceeds the OptiX launch limit");
    auto const launchSize = static_cast<std::uint32_t>(pixelCount * samples);

    impl_->deviceContext.selectDevice();

    // Film changes resize the product entry. A matching Camera::Impl keeps its
    // accumulation.
    auto &entry = impl_->renderProducts.getOrCreate(product);
    auto const accumulatedSamples = entry.accumulation && entry.accumulation->camera == camera
                                        ? entry.accumulation->samples
                                        : 0;
    if (accumulatedSamples > std::numeric_limits<std::uint64_t>::max() - samples)
        throw std::invalid_argument("OptixHandler: accumulated sample count overflows");
    auto const lastBatchIndex = static_cast<std::uint64_t>(samples - 1);
    if (accumulatedSamples > std::numeric_limits<std::uint64_t>::max() - impl_->sampleOffset ||
        accumulatedSamples + impl_->sampleOffset >
            std::numeric_limits<std::uint64_t>::max() - lastBatchIndex)
        throw std::invalid_argument("OptixHandler: sample sequence index overflows");

    try {
        // Clear accumulation before starting work. The next batch clears
        // partial pixels after a backend failure.
        entry.accumulation.reset();
        auto const totalSamples = accumulatedSamples + samples;
        if (accumulatedSamples == 0) {
            entry.storage.forEach([](auto &channel) { channel.buffer.zero(); });
        } else if (entry.requestedChannels != FilmChannels::None) {
            auto const factor =
                static_cast<float>(accumulatedSamples) / static_cast<float>(totalSamples);
            launchLinearKernel(
                pixelCount, ScaleFilmChannels{.film = entry.film, .factor = factor},
                impl_->getStream()
            );
        }

        // The stream orders normalization, parameter upload, and OptiX work.
        // Synchronization below also closes the host lifetime of launch data.
        auto const params = OptixLaunchParams{
            .scene = impl_->optixContext.getImpl(),
            .camera = camera,
            .sampler = sampler->getImpl(resolution),
            .accumulatedSamples = accumulatedSamples,
            .sampleOffset = impl_->sampleOffset,
            .film = entry.film,
            .batchSize = samples,
        };
        impl_->launch(params, launchSize);
        cudaCheck(cudaStreamSynchronize(impl_->getStream()));

        // Publish accumulation after all Film writes complete.
        entry.accumulation = OptixRenderProductPool::AccumulationState{
            .camera = camera,
            .samples = totalSamples,
        };
    } catch (...) {
        entry.accumulation.reset();
        cudaCheck<false>(cudaStreamSynchronize(impl_->getStream()));
        throw;
    }
}

void OptixHandler::download(RenderProduct &product) {
    if (getAccumulatedSamples(product) == 0)
        throw kira::Anyhow("OptixHandler: render product has no valid accumulation");

    impl_->deviceContext.selectDevice();
    auto *entry = impl_->renderProducts.find(product);
    assert(entry);
    auto destination = product.getFilm().prepareDownload();
    try {
        entry->storage.forEach([&](auto &channel) {
            using Channel = typename std::remove_reference_t<decltype(channel)>::ChannelType;
            auto *output = destination.channels.template get<Channel>().data;
            channel.buffer.copyToHost({output, channel.buffer.size()}, impl_->getStream());
        });
        cudaCheck(cudaStreamSynchronize(impl_->getStream()));
    } catch (...) {
        cudaCheck<false>(cudaStreamSynchronize(impl_->getStream()));
        throw;
    }
}

void OptixHandler::setSampleOffset(std::uint64_t offset) {
    if (impl_->sampleOffset == offset)
        return;
    impl_->sampleOffset = offset;
    impl_->renderProducts.resetAccumulation();
}

std::uint64_t OptixHandler::getAccumulatedSamples(RenderProduct const &product) const {
    auto const *entry = impl_->renderProducts.find(product);
    auto const &film = product.getFilm();
    if (!entry || entry->film.width != film.getWidth() || entry->film.height != film.getHeight() ||
        entry->requestedChannels != film.getChannels() || !entry->accumulation)
        return 0;

    auto const camera = product.getCamera().getImpl();
    return entry->accumulation->camera == camera ? entry->accumulation->samples : 0;
}

bool OptixHandler::isConverged(RenderProduct const &product) const {
    return getAccumulatedSamples(product) >= product.getSamplesPerPixel();
}

void OptixHandler::release(RenderProduct const &product) noexcept {
    impl_->deviceContext.selectDevice<false>();
    impl_->renderProducts.erase(product);
}

Ref<Context> OptixHandler::getContext() const { return impl_->context; }
} // namespace flux
