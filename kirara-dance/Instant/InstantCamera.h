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
    [[nodiscard]] krd::float4x4 getViewMatrix() const;

    ///
    [[nodiscard]] krd::float4x4 getProjectionMatrix(float aspectRatio) const;

private:
    krd::float3 position{};
    krd::float3 upDirection{};
    krd::float3 target{};
};
} // namespace krd
