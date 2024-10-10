#include "Instant/InstantObject.h"

#include "Instant/InstantScene.h"
#include "Instant/SlangGraphicsContext.h"

namespace krd {
InstantObject::InstantObject(InstantScene *instantScene) noexcept
    : instantScene(instantScene),
      instantSceneId(instantScene->registerInstantObject(Ref<InstantObject>(this))) {}

Scene *InstantObject::getScene() const {
    KRD_ASSERT(getInstantScene()->getScene(), "InstantObject: Scene is not set");
    return getInstantScene()->getScene().get();
}
} // namespace krd
