#pragma once

#include <pxr/imaging/hd/renderDelegate.h>

#include <filesystem>

#include "flux/Core/Object.h"
#include "flux/Hydra/MaterialXGenerator.h"
#include "flux/Optix/OptixHandler.h"

namespace flux {
class BSDF;
class Context;
} // namespace flux

/// \brief The instance held by the delegate to share with its passes and prims.
class HdFluxRenderParam final : public pxr::HdRenderParam {
public:
    /// \brief Creates a param that renders with the OptiX IR module at \p modulePath.
    ///
    /// \throw std::exception If the OptiX backend cannot start.
    explicit HdFluxRenderParam(std::filesystem::path const &modulePath);
    ~HdFluxRenderParam() override;

    /// \brief Returns the scene.
    [[nodiscard]] flux::Ref<flux::Context> GetContext() const;

    /// \brief Returns the BSDF of a prim that has no material.
    [[nodiscard]] flux::Ref<flux::BSDF const> GetFallbackBSDF() const;

    /// \brief Returns the generator of the MaterialX materials.
    [[nodiscard]] HdFluxMaterialXGenerator const &GetMaterialXGenerator() const noexcept {
        return materialXGenerator_;
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
    flux::Ref<flux::BSDF const> fallbackBSDF_;
    HdFluxMaterialXGenerator materialXGenerator_;
    bool failed_{false};
};
