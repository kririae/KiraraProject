#pragma once

#include <range/v3/view/single.hpp>

#include "Core/Math.h"
#include "SceneGraph/Group.h"
#include "SceneGraph/NodeMixin.h"

namespace krd {
class Transform : public NodeMixin<Transform, Node> {
public:
    [[nodiscard]] static Ref<Transform> create() { return {new Transform}; }

public:
    ///
    void setTranslation(float3 const &translation) { this->translation = translation; }
    ///
    [[nodiscard]] auto const &getTranslation() const { return translation; }

    ///
    void setRotation(float4 const &rotation) { this->rotation = rotation; }
    ///
    [[nodiscard]] auto const &getRotation() const { return rotation; }

    ///
    void setScaling(float3 const &scaling) { this->scaling = scaling; }
    ///
    [[nodiscard]] auto const &getScaling() const { return scaling; }

    /// Returns the TRS transformation matrix.
    [[nodiscard]] float4x4 getMatrix() const;

    ranges::any_view<Ref<Node>> traverse(Visitor &visitor) override {
        return ranges::views::single(children);
    }
    ranges::any_view<Ref<Node>> traverse(ConstVisitor &visitor) const override {
        return ranges::views::single(children);
    }

    /// Add a child node to this transform node.
    void addChild(Ref<Node> child) { children->addChild(std::move(child)); }

protected:
#if 1
    Transform() = default;
    ///
    Transform(float3 const &translation, float4 const &rotation, float3 const &scaling)
        : translation(translation), rotation(rotation), scaling(scaling) {}
#endif

    /// The children of this transform.
    Ref<Group> children{Group::create()};

private:
    // By default, TRS order is used.
    float3 translation{0.0f};
    float4 rotation{0.0f, 0.0f, 0.0f, 1.0f}; // quaternion
    float3 scaling{1.0f};
};
} // namespace krd
