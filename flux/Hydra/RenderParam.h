#pragma once

#include <pxr/imaging/hd/renderDelegate.h>

#include <filesystem>

#include "flux/Core/Object.h"
#include "flux/Optix/OptixHandler.h"
#include "flux/Shading/Mtlx/MtlxShaderGenerator.h"

namespace flux {
class BSDF;
class Context;
class ShaderBSDF;
} // namespace flux

/// \brief The state that the delegate shares with its passes and prims.
class HdFluxRenderParam final : public pxr::HdRenderParam {
public:
    /// \brief Creates a param that renders with the OptiX IR module at \p modulePath.
    ///
    /// \throw std::exception If the OptiX backend or the shader generator cannot start.
    HdFluxRenderParam(
        std::filesystem::path const &modulePath, std::filesystem::path const &mtlxRoot
    );
    ~HdFluxRenderParam() override;

    /// \brief Returns the scene.
    [[nodiscard]] flux::Ref<flux::Context> GetContext() const;

    [[nodiscard]] flux::Ref<flux::BSDF const> GetFallbackBSDF() const;

    [[nodiscard]] flux::ShaderInstance const &GetFallbackShaderInstance() const noexcept;

    [[nodiscard]] flux::MtlxShaderGenerator const &GetShaderGenerator() const noexcept {
        return shaderGenerator_;
    }

    /// \brief Returns the backend that renders the scene.
    [[nodiscard]] flux::OptixHandler &GetHandler() noexcept { return handler_; }

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
    bool failed_{false};
};
