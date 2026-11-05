#include "flux/Scene/TXContext.h"

#include "flux/Scene/Context.h"

namespace flux {
TXContext::TXContext(Context &context) noexcept : context_(context) {}

std::size_t TXContext::allocateId() { return context_.allocateId(); }

Ref<ContextObject> TXContext::getObject(std::size_t contextId) const {
    if (auto const iterator = objects_.find(contextId); iterator != objects_.end())
        return iterator->second;
    return context_.get<ContextObject>(contextId);
}

bool TXContext::contains(ContextObject const &object) const {
    if (object.getContext() == &context_)
        return true;
    auto const iterator = objects_.find(object.getContextId());
    return iterator != objects_.end() && iterator->second.get() == &object;
}

kira::FileResolver const &TXContext::getFileResolver() const noexcept {
    return context_.getFileResolver();
}

ImageAssetPool &TXContext::getImageAssetPool() const noexcept {
    return context_.getImageAssetPool();
}

ShaderAssetPool &TXContext::getShaderAssetPool() const noexcept {
    return context_.getShaderAssetPool();
}

void TXContext::registerObject(Ref<ContextObject> object) {
    auto const contextId = object->getContextId();
    auto const [iterator, inserted] = objects_.emplace(contextId, std::move(object));
    KIRA_ASSERT(inserted, "TXContext: context IDs are unique");

    try {
        if (auto const kind = iterator->second->getIndexedKind())
            indices_[static_cast<std::size_t>(*kind)].insert(contextId);
    } catch (...) {
        objects_.erase(iterator);
        throw;
    }
}
} // namespace flux
