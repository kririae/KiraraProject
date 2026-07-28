#include <cuda_runtime_api.h>
#include <optix_function_table_definition.h>
#include <optix_stubs.h>

#include <cstdint>
#include <limits>
#include <stdexcept>
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
#include "flux/Scene/Film.cuh"
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
    Film::DeviceImpl film;
    float factor;

    KIRA_DEVICE void operator()(std::size_t index) const noexcept { film.scale(index, factor); }
};
} // namespace

struct OptixHandler::Impl final {
    Impl(Ref<Context> hostContext, std::filesystem::path const &modulePath);
    ~Impl();

    /// \brief Rebuilds the device scene from the host context.
    void sync();

    /// \brief Uploads \p params and launches a one-dimensional grid.
    void launch(OptixLaunchParams const &params, std::uint32_t size);

    /// \brief Returns the stream shared by this backend's device resources.
    [[nodiscard]] cudaStream_t getStream() const noexcept { return deviceContext.getStream(); }

    /// Host scene retained for the lifetime of the backend.
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

    // A failed scene rebuild invalidates this backend, so no previous target
    // history may remain observable after sync begins.
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

    // Resolve launch-time inputs. None of these objects belong to the
    // persistent OptiX scene.
    auto const &film = product.getFilm();
    auto const resolution = Vec2u{film.getWidth(), film.getHeight()};
    auto const sampler = impl_->context->getActiveSampler();
    auto const camera = product.getCamera().getDeviceImpl();

    // Bound values are baked into the module. A changed host spec needs a new
    // module before its launch data can be used.
    if (impl_->optixContext.getProgramSpec() != OptixProgram::makeSpec(*impl_->context))
        throw kira::Anyhow(
            "OptixHandler: program specialization changed; call sync before rendering"
        );

    // OptiX launches one work item per pixel sample. Samples of the same pixel
    // remain consecutive in the one-dimensional launch.
    auto const pixelCount =
        static_cast<std::uint64_t>(film.getWidth()) * static_cast<std::uint64_t>(film.getHeight());
    if (pixelCount > maxOptixLaunchDimension || samples > maxOptixLaunchDimension / pixelCount)
        throw std::invalid_argument("OptixHandler: render batch exceeds the OptiX launch limit");
    auto const launchSize = static_cast<std::uint32_t>(pixelCount * samples);

    impl_->deviceContext.selectDevice();

    // Film changes update this product's existing entry. Camera compatibility
    // only controls whether its pixel history can be reused.
    auto &target = impl_->renderProducts.getOrCreate(product);
    auto const accumulatedSamples = target.accumulation && target.accumulation->camera == camera
                                        ? target.accumulation->samples
                                        : 0;
    if (accumulatedSamples > std::numeric_limits<std::uint64_t>::max() - samples)
        throw std::invalid_argument("OptixHandler: accumulated sample count overflows");
    auto const lastBatchIndex = static_cast<std::uint64_t>(samples - 1);
    if (accumulatedSamples > std::numeric_limits<std::uint64_t>::max() - impl_->sampleOffset ||
        accumulatedSamples + impl_->sampleOffset >
            std::numeric_limits<std::uint64_t>::max() - lastBatchIndex)
        throw std::invalid_argument("OptixHandler: sample sequence index overflows");

    try {
        // Pixel writes below may leave a partial image if CUDA reports an
        // asynchronous failure. Clear the validity marker before enqueueing
        // them so the next batch starts from a zeroed target.
        target.accumulation.reset();
        auto const totalSamples = accumulatedSamples + samples;
        if (accumulatedSamples == 0) {
            target.storage.forEach([](auto &channel) { channel.buffer.zero(); });
        } else if (target.enabledChannels != FilmChannels::None) {
            auto const factor =
                static_cast<float>(accumulatedSamples) / static_cast<float>(totalSamples);
            launchLinearKernel(
                pixelCount, ScaleFilmChannels{.film = target.film, .factor = factor},
                impl_->getStream()
            );
        }

        // The stream orders normalization, parameter upload, and OptiX work.
        // Synchronization below also closes the host lifetime of launch data.
        auto const params = OptixLaunchParams{
            .scene = impl_->optixContext.getDeviceImpl(),
            .camera = camera,
            .sampler = sampler->getDeviceImpl(resolution),
            .accumulatedSamples = accumulatedSamples,
            .sampleOffset = impl_->sampleOffset,
            .film = target.film,
            .batchSize = samples,
        };
        impl_->launch(params, launchSize);
        cudaCheck(cudaStreamSynchronize(impl_->getStream()));

        // Publish the new history only after all device writes have completed.
        target.accumulation = OptixRenderProductPool::AccumulationState{
            .camera = camera,
            .samples = totalSamples,
        };
    } catch (...) {
        target.accumulation.reset();
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
    auto const *target = impl_->renderProducts.find(product);
    auto const &film = product.getFilm();
    if (!target || target->film.width != film.getWidth() ||
        target->film.height != film.getHeight() || target->enabledChannels != film.getChannels() ||
        !target->accumulation)
        return 0;

    auto const camera = product.getCamera().getDeviceImpl();
    return target->accumulation->camera == camera ? target->accumulation->samples : 0;
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
