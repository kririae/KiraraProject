#pragma once

#include <cuda_runtime_api.h>
#include <optix_types.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <type_traits>

#include "flux/Core/Object.h"
#include "flux/Core/Ray.h"
#include "flux/Sampling/LightSampler.h"
#include "flux/Scene/Primitive.h"
#include "flux/Scene/TriangleMesh.h"
#include "flux/Shading/BSDF.h"
#include "kira/Compiler.h"

namespace flux {
class Context;
class OptixHandler;
struct PathState;
struct OptixProgramSpec;

/// \brief Owns the OptiX scene built from a host \c Context.
class OptixContext final : private Noncopyable {
    friend class OptixHandler;

public:
    /// \brief Device view of the current OptiX scene.
    struct Impl;

    /// \brief Releases OptiX scene resources.
    ~OptixContext();

private:
    /// \brief Creates an empty OptiX scene for \p context.
    ///
    /// \param context Host \c Context borrowed from the owning \c OptixHandler.
    /// \param deviceContext OptiX device context borrowed from the owning
    /// \c OptixHandler.
    /// \param stream CUDA stream that orders OptiX scene builds and launches.
    /// \param modulePath Path to the OptiX IR module used by \c OptixProgram.
    /// \throw kira::Anyhow if the module cannot be read or OptiX setup fails.
    OptixContext(
        Context &context, OptixDeviceContext deviceContext, cudaStream_t stream,
        std::filesystem::path const &modulePath
    );

    /// \brief Rebuilds the OptiX scene from the current host \c Context.
    ///
    /// This function fully rebuilds the OptiX scene and waits for queued work
    /// before returning. Destroy this context after a failed rebuild.
    /// \throw kira::Anyhow If scene linking, CUDA, or OptiX setup fails.
    /// \throw std::out_of_range If a primitive refers to an unknown geometry.
    void sync();

    /// \brief Launches the persistent pipeline as a one-dimensional grid.
    ///
    /// \param stream CUDA stream that orders the launch.
    /// \param params Device address of the launch parameters.
    /// \param paramsSize Size of the launch parameters in bytes.
    /// \param size Number of ray-generation work items.
    void launch(
        cudaStream_t stream, CUdeviceptr params, std::size_t paramsSize, std::uint32_t size
    ) const;

    /// \brief Returns the current OptiX scene view.
    [[nodiscard]] Impl getImpl() const noexcept;

    /// \brief Returns the values specialized into the current pipeline.
    [[nodiscard]] OptixProgramSpec const &getProgramSpec() const noexcept;

    struct Storage;
    std::unique_ptr<Storage> storage_;
};

/// \brief Device view of an OptiX scene.
struct OptixContext::Impl {
    /// Top-level instance acceleration structure.
    OptixTraversableHandle traversable{};

    /// Device array of unique triangle meshes.
    TriangleMesh::Impl const *geometries{};

    /// Device array of visible primitives.
    Primitive::Impl const *primitives{};

    /// Device array of BSDFs registered in the host \c Context.
    BSDF::Impl const *bsdfs{};

    /// Borrowed light sampler.
    LightSampler lightSampler{};

    /// Number of elements in \c geometries.
    std::uint32_t numGeometries{};

    /// Number of elements in \c primitives.
    std::uint32_t numPrimitives{};

    /// Number of elements in \c bsdfs.
    std::uint32_t numBSDFs{};

public:
    /// \brief Traces the ray in \p state.
    ///
    /// An empty scene takes the same transition as a miss. Otherwise, the
    /// selected miss or closest-hit program advances \p state.
    KIRA_DEVICE void trace(PathState &state) const noexcept;

    /// \brief Returns whether \p ray reaches its endpoint without obstruction.
    [[nodiscard]] KIRA_DEVICE bool isVisible(Ray const &ray) const noexcept;

    /// \brief Returns the primitive at dense \p instanceIndex.
    ///
    /// \pre \p instanceIndex is less than \c numPrimitives.
    [[nodiscard]] KIRA_DEVICE inline Primitive::Impl const &
    getPrimitive(std::uint32_t instanceIndex) const noexcept;

    /// \brief Returns the geometry at dense \p geometryIndex.
    ///
    /// \pre \p geometryIndex is less than \c numGeometries.
    [[nodiscard]] KIRA_DEVICE inline TriangleMesh::Impl const &
    getGeometry(std::uint32_t geometryIndex) const noexcept;

    /// \brief Returns the BSDF at dense \p bsdfIndex.
    ///
    /// \pre \p bsdfIndex is less than \c numBSDFs.
    [[nodiscard]] KIRA_DEVICE inline BSDF::Impl const &
    getBSDF(std::uint32_t bsdfIndex) const noexcept;

    /// \brief Returns the light sampler built with this scene.
    [[nodiscard]] KIRA_DEVICE inline LightSampler const &getLightSampler() const noexcept {
        return lightSampler;
    }
};

static_assert(std::is_standard_layout_v<OptixContext::Impl>);
static_assert(std::is_trivially_copyable_v<OptixContext::Impl>);

namespace optix {
/// OptiX device scene view.
using Scene = ::flux::OptixContext::Impl;
} // namespace optix
} // namespace flux
