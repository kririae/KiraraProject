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

kira::FileResolver const &TXContext::getFileResolver() const noexcept {
    return context_.getFileResolver();
}

ImageAssetPool &TXContext::getImageAssetPool() const noexcept {
    return context_.getImageAssetPool();
}

void TXContext::registerObject(Ref<ContextObject> object) {
    auto const contextId = object->getContextId();
    auto const [iterator, inserted] = objects_.emplace(contextId, std::move(object));
    if (!inserted)
        throw kira::Anyhow("TXContext: object ID is already registered");

    try {
        if (auto const kind = iterator->second->getIndexedKind())
            indices_[static_cast<std::size_t>(*kind)].insert(contextId);
    } catch (...) {
        objects_.erase(iterator);
        throw;
    }
}
} // namespace flux
