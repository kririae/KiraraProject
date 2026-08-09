#include "flux/Scene/TXContext.h"

#include "flux/Scene/Context.h"
#include "flux/Shading/BSDF.h"
#include "flux/Shading/EDF.h"
#include "flux/Shading/Texture.h"

namespace flux {
TXContext::TXContext(Context &context) noexcept : context_(context) {}

std::size_t TXContext::allocateId() { return context_.allocateId(); }

Ref<ContextObject> TXContext::getObject(std::size_t contextId) const {
    if (auto const iterator = objects_.find(contextId); iterator != objects_.end())
        return iterator->second;
    return context_.get<ContextObject>(contextId);
}

void TXContext::registerObject(Ref<ContextObject> object) {
    if (object->getContext() != &context_)
        throw kira::Anyhow("TXContext: object belongs to another context");

    auto const contextId = object->getContextId();
    auto const [iterator, inserted] = objects_.emplace(contextId, std::move(object));
    if (!inserted)
        throw kira::Anyhow("TXContext: object ID is already registered");

    try {
        if (dynamic_cast<ImageTexture *>(iterator->second.get()))
            imageTextures_.insert(contextId);
        else if (dynamic_cast<BSDF *>(iterator->second.get()))
            bsdfs_.insert(contextId);
        else if (dynamic_cast<EDF *>(iterator->second.get()))
            edfs_.insert(contextId);
    } catch (...) {
        objects_.erase(iterator);
        throw;
    }
}

void TXContext::stageActiveIntegrator(std::size_t contextId) noexcept {
    if (!activeIntegratorId_)
        activeIntegratorId_ = contextId;
}

void TXContext::stageActiveSampler(std::size_t contextId) noexcept {
    if (!activeSamplerId_)
        activeSamplerId_ = contextId;
}
} // namespace flux
