#include "flux/Hydra/Instancer.h"

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/quatd.h>
#include <pxr/base/gf/quatf.h>
#include <pxr/base/gf/quath.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/changeTracker.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>

#include <algorithm>
#include <cstddef>
#include <vector>

#include "flux/Scene/Context.h"
#include "flux/Scene/Primitive.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
template <typename Array>
[[nodiscard]] Array toArray(SdfPath const &id, TfToken const &name, VtValue const &value) {
    if (value.IsEmpty())
        return {};

    auto const cast = VtValue::Cast<Array>(value);
    if (cast.IsEmpty()) {
        TF_WARN(
            "%s: the primvar %s holds %s, which hdFlux does not read, so it is ignored",
            id.GetText(), name.GetText(), value.GetTypeName().c_str()
        );
        return {};
    }
    return cast.template UncheckedGet<Array>();
}

[[nodiscard]] VtQuatdArray
toRotations(SdfPath const &id, TfToken const &name, VtValue const &value) {
    if (value.IsEmpty())
        return {};

    if (value.IsHolding<VtQuathArray>()) {
        auto const &quaternions = value.UncheckedGet<VtQuathArray>();
        return {quaternions.begin(), quaternions.end()};
    }
    if (value.IsHolding<VtQuatfArray>()) {
        auto const &quaternions = value.UncheckedGet<VtQuatfArray>();
        return {quaternions.begin(), quaternions.end()};
    }

    auto const vectors = toArray<VtVec4dArray>(id, name, value);
    VtQuatdArray rotations(vectors.size());
    std::ranges::transform(vectors, rotations.begin(), [](GfVec4d const &vector) {
        return GfQuatd{vector[0], vector[1], vector[2], vector[3]};
    });
    return rotations;
}

template <typename Array> [[nodiscard]] bool covers(Array const &array, int index) {
    return array.empty() || static_cast<std::size_t>(index) < array.size();
}
} // namespace

HdFluxInstancer::HdFluxInstancer(HdSceneDelegate *sceneDelegate, SdfPath const &id)
    : HdInstancer(sceneDelegate, id) {}

void HdFluxInstancer::Sync(
    HdSceneDelegate *sceneDelegate, HdRenderParam *, HdDirtyBits *dirtyBits
) {
    auto const &id = GetId();

    _UpdateInstancer(sceneDelegate, dirtyBits);

    if (!HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, id))
        return;

    auto const descriptors = sceneDelegate->GetPrimvarDescriptors(id, HdInterpolationInstance);
    auto const get = [&](TfToken const &name) {
        auto const described =
            std::ranges::any_of(descriptors, [&](HdPrimvarDescriptor const &descriptor) {
            return descriptor.name == name;
        });
        return described ? sceneDelegate->Get(id, name) : VtValue{};
    };

    auto const &tokens = HdInstancerTokens;
    translations_ =
        toArray<VtVec3dArray>(id, tokens->instanceTranslations, get(tokens->instanceTranslations));
    rotations_ = toRotations(id, tokens->instanceRotations, get(tokens->instanceRotations));
    scales_ = toArray<VtVec3dArray>(id, tokens->instanceScales, get(tokens->instanceScales));
    transforms_ =
        toArray<VtMatrix4dArray>(id, tokens->instanceTransforms, get(tokens->instanceTransforms));
}

VtMatrix4dArray HdFluxInstancer::ComputeInstanceTransforms(SdfPath const &prototypeId) const {
    auto const &id = GetId();
    auto &sceneDelegate = *GetDelegate();

    auto const indices = sceneDelegate.GetInstanceIndices(id, prototypeId);
    auto const wellFormed = std::ranges::all_of(indices, [&](int index) {
        return covers(translations_, index) && covers(rotations_, index) &&
               covers(scales_, index) && covers(transforms_, index);
    });
    if (!wellFormed) {
        TF_WARN(
            "%s: an instance index of %s is outside the instance primvars, so it has no instances",
            id.GetText(), prototypeId.GetText()
        );
        return {};
    }

    auto const instancerTransform = sceneDelegate.GetInstancerTransform(id);
    VtMatrix4dArray transforms(indices.size());
    std::ranges::transform(indices, transforms.begin(), [&](int index) {
        auto transform = transforms_.empty() ? GfMatrix4d{1.0} : transforms_[index];
        if (!scales_.empty())
            transform *= GfMatrix4d{}.SetScale(scales_[index]);
        if (!rotations_.empty())
            transform *= GfMatrix4d{}.SetRotate(rotations_[index]);
        if (!translations_.empty())
            transform *= GfMatrix4d{}.SetTranslate(translations_[index]);
        return transform * instancerTransform;
    });

    if (GetParentId().IsEmpty())
        return transforms;

    auto *const parent = sceneDelegate.GetRenderIndex().GetInstancer(GetParentId());
    if (!TF_VERIFY(parent))
        return {};

    auto const parentTransforms =
        static_cast<HdFluxInstancer const *>(parent)->ComputeInstanceTransforms(id);
    VtMatrix4dArray nested;
    nested.reserve(parentTransforms.size() * transforms.size());
    for (auto const &parentTransform : parentTransforms)
        for (auto const &transform : transforms)
            nested.push_back(transform * parentTransform);
    return nested;
}

VtMatrix4dArray HdFluxComputeInstanceTransforms(
    HdRenderIndex &renderIndex, SdfPath const &instancerId, SdfPath const &prototypeId
) {
    if (instancerId.IsEmpty())
        return {GfMatrix4d{1.0}};

    HdInstancer::_SyncInstancerAndParents(renderIndex, instancerId);

    auto *const instancer = renderIndex.GetInstancer(instancerId);
    if (!TF_VERIFY(instancer))
        return {};
    return static_cast<HdFluxInstancer const *>(instancer)->ComputeInstanceTransforms(prototypeId);
}

void HdFluxTruncatePrimitives(
    flux::Context &context, std::vector<flux::Ref<flux::Primitive>> &primitives, std::size_t count
) {
    while (primitives.size() > count) {
        context.remove(primitives.back()->getContextId());
        primitives.pop_back();
    }
}
