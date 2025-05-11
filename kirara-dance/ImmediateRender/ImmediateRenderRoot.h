#pragma once

#include "SceneGraph/NodeMixin.h"

namespace krd {
class ImmediateRenderRoot : public NodeMixin<ImmediateRenderRoot, Node> {
public:
    [[nodiscard]] static Ref<ImmediateRenderRoot> create() { return {new ImmediateRenderRoot}; }
    ~ImmediateRenderRoot() override = default;

public:
};
} // namespace krd
