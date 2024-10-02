#pragma once

#include "Core/Object.h"

namespace krd {
class RenderScene;

class RenderObject : public Object {
public:
    ~RenderObject() override = default;

protected:
    RenderObject(RenderScene *renderScene);

private:
    RenderScene *renderScene{nullptr};
};
} // namespace krd
