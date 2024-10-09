#pragma once

#include "Core/Math.h"
#include "FLux/RenderObject.h"

namespace krd {
class RenderCamera final : public RenderObject {
    explicit RenderCamera(FLuxScene *flScene);

public:
    ///
    static Ref<RenderCamera> create(FLuxScene *renderScene) {
        return {new RenderCamera(renderScene)};
    }

    /// \see RenderObject::pull
    bool pull() override;

    ///
    [[nodiscard]] krd::float4x4 getViewMatrix() const;

    ///
    [[nodiscard]] krd::float4x4 getProjectionMatrix(float aspectRatio) const;

private:
    krd::float3 position{};
    krd::float3 upDirection{};
    krd::float3 target{};
};
} // namespace krd
