#include "flux/Scene/LightTableData.h"

#include <algorithm>
#include <cstddef>

#include "flux/Core/Logging.h"
#include "flux/Scene/Context.h"
#include "flux/Scene/SceneTableData.h"
#include "kira/Anyhow.h"

namespace flux {
void LightTableData::build(
    SceneTableData const &scene, Context const &context, std::optional<float> envMapPower,
    std::uint32_t maxSlots
) {
    clear();

    // Bound the cap. A larger one would break the CDF precision.
    maxSlots = std::min(maxSlots, LightPowerDistribution::maxLightCount);

    // Size the arrays.
    auto const contextLights = context.getObjects<Light>();
    auto const numPrimitives = scene.primitives.size();
    lights.points.reserve(contextLights.size());
    lights.primAreaScales.assign(numPrimitives, 0.0F);
    slots.handles.reserve(contextLights.size() + numPrimitives);
    slots.primSlots.assign(numPrimitives, invalidSlot);

    // Give a light the next slot, or none past the cap. Powers only feed the CDF.
    std::vector<float> powers;
    powers.reserve(contextLights.size() + numPrimitives);
    std::size_t numDropped = 0;
    auto const addSlot = [&](LightHandle handle, float power) {
        if (slots.handles.size() >= maxSlots) {
            ++numDropped;
            return invalidSlot;
        }

        auto const slot = static_cast<std::uint32_t>(slots.handles.size());
        slots.handles.push_back(handle);
        powers.push_back(power);
        return slot;
    };

    // Assign slots to point lights first. BSDF sampling never hits a point light,
    // so the cap must drop an emitter instead.
    for (auto const &light : contextLights) {
        switch (light->getType()) {
        case LightType::Point: {
            auto point = light.dynamicCast<PointLight const>();
            if (!point)
                throw kira::Anyhow(
                    "LightTableData: light type does not match its host implementation"
                );

            auto const pointIndex = static_cast<std::uint32_t>(lights.points.size());
            lights.points.push_back(point->getImpl());
            (void)addSlot({.type = LightType::Point, .index = pointIndex}, point->estimatePower());
            break;
        }
        case LightType::EnvMap: break;
        case LightType::Primitive:
        case LightType::Count: throw kira::Anyhow("LightTableData: unsupported light type");
        }
    }

    // Assign a slot to the environment map. A ray miss finds it, so it ranks before
    // emitters for the same reason.
    if (envMapPower)
        slots.envMapSlot = addSlot({.type = LightType::EnvMap, .index = 0}, *envMapPower);

    // Assign slots to visible primitives with an EDF, in index order. An emitter past
    // the cap stays reachable by BSDF sampling, where MIS weights it one.
    for (std::size_t index = 0; index < numPrimitives; ++index) {
        if (scene.primitives[index].isHole())
            continue;

        auto const &primitive = scene.objects.primitives[index];
        lights.primAreaScales[index] = primitive->estimateAreaScale();
        if (!scene.primitives[index].hasEDF())
            continue;

        if (primitive->hasNonUniformScale())
            LogWarn(
                "LightTableData: emissive primitive {} has non-uniform scale; using its average "
                "scale for light sampling",
                primitive->getContextId()
            );
        auto const primIndex = static_cast<std::uint32_t>(index);
        slots.primSlots[index] =
            addSlot({.type = LightType::Primitive, .index = primIndex}, primitive->estimatePower());
    }

    // Report lights past the cap.
    if (numDropped > 0)
        LogWarn(
            "LightTableData: {} lights exceed the limit of {} and cannot be sampled directly",
            numDropped, maxSlots
        );

    // Build the CDF over the assigned slots.
    slots.cdf = buildLightPowerCDF(powers);
}

void LightTableData::clear() noexcept {
    lights.points.clear();
    lights.primAreaScales.clear();
    slots.handles.clear();
    slots.cdf.clear();
    slots.primSlots.clear();
    slots.envMapSlot = invalidSlot;
}
} // namespace flux
