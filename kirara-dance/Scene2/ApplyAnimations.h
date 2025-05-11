#pragma once

#include "SceneGraph/Visitors.h"

namespace krd {
class ApplyAnimations : public Visitor {
public:
    [[nodiscard]] static Ref<ApplyAnimations> create() { return {new ApplyAnimations}; }

public:
    void apply(Node &t) override { traverse(t); }
};
} // namespace krd
