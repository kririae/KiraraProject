#include "Renderer/RenderObject.h"

#include "Renderer/RenderScene.h"
#include "Renderer/SlangGraphicsContext.h"

namespace krd {
RenderObject::RenderObject(RenderScene *renderScene)
    : renderScene(renderScene),
      renderSceneId(renderScene->registerRenderObject(Ref<RenderObject>(this))) {}

Scene *RenderObject::getScene() const {
    KRD_ASSERT(getRenderScene()->getScene(), "RenderObject: Scene is not set");
    return getRenderScene()->getScene().get();
}

SlangGraphicsContext *RenderObject::getGraphicsContext() const {
    KRD_ASSERT(getRenderScene()->getGraphicsContext(), "RenderObject: GraphicsContext is not set");
    return getRenderScene()->getGraphicsContext().get();
}
} // namespace krd
