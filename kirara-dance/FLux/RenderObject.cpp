#include "FLux/RenderObject.h"

#include "FLux/FLuxScene.h"
#include "FLux/SlangGraphicsContext.h"

namespace krd {
RenderObject::RenderObject(FLuxScene *renderScene)
    : renderScene(renderScene),
      renderSceneId(renderScene->registerRenderObject(Ref<RenderObject>(this))) {}

Scene *RenderObject::getScene() const {
    KRD_ASSERT(getRenderScene()->getScene(), "RenderObject: Scene is not set");
    return getRenderScene()->getScene().get();
}

SlangGraphicsContext *RenderObject::getGraphicsContext() const {
    KRD_ASSERT(
        getRenderScene()->getGraphicsContext(), "RenderObject: GraphicsContext is not yet bind"
    );
    return getRenderScene()->getGraphicsContext();
}
} // namespace krd
