#pragma once

#include "Core/Math.h"
#include "Instant/InstantObject.h"

namespace krd {
class InstantCamera final : public InstantObject {
    explicit InstantCamera(InstantScene *iScene);

public:
    ///
    static Ref<InstantCamera> create(InstantScene *iScene) { return {new InstantCamera(iScene)}; }

    /// \see InstantObject::pull
    bool pull() override;

    ///
    [[nodiscard]] float4x4 getViewMatrix() const;

    ///
    [[nodiscard]] float4x4 getProjectionMatrix(float aspectRatio) const;

private:
    float3 position{};
    float3 upDirection{};
    float3 target{};
};
} // namespace krd
