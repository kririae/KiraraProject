#pragma once

#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderThread.h>

#include <atomic>
#include <filesystem>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Optix/OptixHandler.h"
#include "flux/Shading/Mtlx/MtlxShaderGenerator.h"

namespace flux {
class BSDF;
class Context;
class ShaderBSDF;
} // namespace flux

struct HdFLuxPendingMaterial;

/// \brief The state that the delegate shares with its passes and prims.
class HdFLuxRenderParam final : public pxr::HdRenderParam {
public:
    /// \pre The render thread outlives the param.
    /// \throw std::exception If the OptiX backend or the shader generator cannot start.
    HdFLuxRenderParam(std::filesystem::path const &mtlxRoot, pxr::HdRenderThread &renderThread);
    ~HdFLuxRenderParam() override;

public:
    [[nodiscard]] flux::Ref<flux::Context> GetContext() const;

    /// \brief Returns the backend that renders the scene.
    [[nodiscard]] flux::OptixHandler &GetHandler() noexcept { return handler_; }

public:
    [[nodiscard]] flux::Ref<flux::BSDF const> GetFallbackBSDF() const;

    [[nodiscard]] flux::ShaderInstance const &GetFallbackShaderInstance() const noexcept;

    [[nodiscard]] flux::MtlxShaderGenerator const &GetShaderGenerator() const noexcept {
        return shaderGenerator_;
    }

    void AddPendingMaterial(HdFLuxPendingMaterial material);

    /// \brief Returns the pending materials, and leaves none pending.
    [[nodiscard]] std::vector<HdFLuxPendingMaterial> ExtractPendingMaterials() noexcept;

public:
    void StopRender();

public:
    /// \brief Returns whether a failure was reported.
    ///
    /// Once true, it stays true.
    [[nodiscard]] bool HasFailed() const noexcept { return failed_; }

    /// \brief Logs the exception that is being handled and makes the param failed for good.
    ///
    /// \pre It is called from a catch block.
    void ReportFailure() noexcept;

private:
    flux::OptixHandler handler_;

    flux::MtlxShaderGenerator shaderGenerator_;
    flux::ShaderInstance fallbackShaderInstance_;
    flux::Ref<flux::ShaderBSDF> fallbackBSDF_;
    std::vector<HdFLuxPendingMaterial> pendingMaterials_;

    pxr::HdRenderThread &renderThread_;

    std::atomic<bool> failed_{false};
};
