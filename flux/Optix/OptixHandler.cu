#include <cuda_runtime_api.h>
#include <optix_function_table_definition.h>
#include <optix_stubs.h>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

#include "flux/Core/Logging.h"
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
/// \brief Estimates how many megakernel threads \p properties keeps resident.
///
/// OptiX exposes no occupancy query for a pipeline, so this derives the count from the register
/// budget the module is compiled against. It reports the estimate alone; how a launch is sized
/// against it is the caller's choice.
[[nodiscard]] std::uint32_t estimateResidentThreads(cudaDeviceProp const &properties) noexcept {
    auto const perSM = std::min(
        properties.maxThreadsPerMultiProcessor,
        static_cast<int>(properties.regsPerMultiprocessor / megakernelMaxRegisterCount)
    );
    return static_cast<std::uint32_t>(properties.multiProcessorCount) *
           static_cast<std::uint32_t>(std::max(perSM, 1));
}

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
        cudaDeviceProp deviceProperties{};
        cudaCheck(cudaGetDeviceProperties(&deviceProperties, deviceId));
        LogInfo("OptixHandler: using CUDA device {} ('{}')", deviceId, deviceProperties.name);
        estimatedResidentThreads_ = estimateResidentThreads(deviceProperties);
        cudaCheck(cudaStreamCreateWithFlags(&stream_, cudaStreamNonBlocking));
        optixCheck(optixInit());

        OptixDeviceContextOptions options{};
        optixCheck(optixDeviceContextCreate(nullptr, &options, &context_));
        optixCheck(optixDeviceContextSetLogCallback(
            context_, +[](unsigned int level, char const *tag, char const *message, void *) {
            auto text = std::string_view{message};
            while (text.ends_with('\n'))
                text.remove_suffix(1);
            if (text.empty())
                return;

            // OptiX build calls return compiler errors through their log buffer.
            if (level <= 2 && std::string_view{tag} == "COMPILER")
                return;

            switch (level) {
            case 1:
            case 2: LogError("OptiX [{}]: {}", tag, text); break;
            case 3: LogWarn("OptiX [{}]: {}", tag, text); break;
            default: LogDebug("OptiX [{}]: {}", tag, text); break;
            }
        }, nullptr, 4
        ));
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

    /// \brief Returns the estimated number of megakernel threads the device keeps resident.
    [[nodiscard]] std::uint32_t getEstimatedResidentThreads() const noexcept {
        return estimatedResidentThreads_;
    }

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
    std::uint32_t estimatedResidentThreads_{};
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

    /// \brief Returns how many threads to launch for \p pathCount paths.
    ///
    /// Lanes claim the paths the launch does not cover, so this only decides how many threads
    /// share the work. Launching past the estimated residency keeps an underestimate from
    /// leaving part of the device idle for the whole launch; launching under it trades threads
    /// for a smaller resident set of continuation frames.
    [[nodiscard]] std::uint32_t getLaunchSize(std::uint32_t pathCount) const noexcept {
        auto const scaled =
            static_cast<double>(deviceContext.getEstimatedResidentThreads()) * launchWaves;
        auto const threads = static_cast<std::uint32_t>(std::lround(std::max(scaled, 1.0)));
        return std::min(pathCount, std::max(threads, 1U));
    }

    /// Host \c Context retained for the lifetime of the backend.
    Ref<Context> context;

    /// CUDA execution state destroyed after every dependent resource.
    OptixDeviceContextHandle deviceContext;

    OptixContext optixContext;
    OptixRenderProductPool renderProducts{getStream()};
    DeviceBuffer<OptixLaunchParams> launchParams{getStream()};

    /// Next path not yet claimed by any lane. Zeroed before each launch.
    DeviceBuffer<std::uint32_t> pathCounter{getStream()};
    CudaStreamTimer timer{getStream()};
    std::uint64_t sampleOffset{};

    /// Threads to launch as a multiple of the estimated resident thread count.
    double launchWaves{2.0};
};

OptixHandler::Impl::Impl(Ref<Context> hostContext, std::filesystem::path const &modulePath)
    : context(requireContext(std::move(hostContext))),
      optixContext(*context, deviceContext.get(), getStream(), modulePath) {
    pathCounter.resize(1);
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

RenderStats OptixHandler::render(RenderProduct const &product, std::uint32_t samples) {
    if (samples == 0)
        throw std::invalid_argument("OptixHandler: sample batch must be nonzero");

    auto const &film = product.getFilm();
    auto const resolution = Vec2u{film.getWidth(), film.getHeight()};
    auto const sampler = impl_->context->getActiveSampler();
    auto const integrator = impl_->context->getActiveIntegrator();
    auto const camera = product.getCamera().getImpl();

    // The module contains bound values. A changed Context spec requires sync
    // before launch.
    if (impl_->optixContext.getProgramSpec() != OptixProgram::makeSpec(*impl_->context))
        throw kira::Anyhow(
            "OptixHandler: program specialization changed; call sync before rendering"
        );

    // One path per pixel sample, with each pixel's samples consecutive. The launch is capped at
    // the resident thread count, so a lane whose path ends claims another rather than retiring
    // and leaving its warp short.
    auto const pixelCount =
        static_cast<std::uint64_t>(film.getWidth()) * static_cast<std::uint64_t>(film.getHeight());
    if (pixelCount > maxOptixLaunchDimension || samples > maxOptixLaunchDimension / pixelCount)
        throw std::invalid_argument("OptixHandler: render batch exceeds the OptiX launch limit");
    auto const pathCount = static_cast<std::uint32_t>(pixelCount * samples);
    auto const launchSize = impl_->getLaunchSize(pathCount);

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
        impl_->timer.start();
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
        impl_->pathCounter.zero();
        auto const &programSpec = impl_->optixContext.getProgramSpec();
        auto const params = OptixLaunchParams{
            .scene = impl_->optixContext.getImpl(),
            .camera = camera,
            .sampler = sampler->getImpl(resolution),
            .integrator = integrator->getImpl(),
            .bsdfDispatcher =
                {
                    .types = programSpec.bsdfTypes, // (2)
                },
            .accumulatedSamples = accumulatedSamples,
            .sampleOffset = impl_->sampleOffset,
            .nextPath = impl_->pathCounter.data(),
            .film = entry.film,
            .batchSize = samples,
            .pathCount = pathCount,
            .shaderReorder = programSpec.shaderReorder, // (3)
            .hasEnvMap = programSpec.hasEnvMap,         // (4)
        };
        impl_->launch(params, launchSize);
        auto const elapsed = impl_->timer.stop();

        // Publish accumulation after all Film writes complete.
        entry.accumulation = OptixRenderProductPool::AccumulationState{
            .camera = camera,
            .samples = totalSamples,
        };
        return {
            .paths = pathCount,
            .elapsed = elapsed,
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

void OptixHandler::setLaunchWaves(double waves) {
    if (!(waves > 0.0))
        throw kira::Anyhow("OptixHandler: launch waves must be positive, got {}", waves);
    impl_->launchWaves = waves;
}

std::uint32_t OptixHandler::getEstimatedResidentThreads() const {
    return impl_->deviceContext.getEstimatedResidentThreads();
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
