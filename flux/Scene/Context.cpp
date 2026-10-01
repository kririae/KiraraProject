#include "flux/Scene/Context.h"

#include "flux/Integrator/PathIntegrator.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/EnvMapLight.h"

namespace flux {
Ref<Context> Context::create() { return Ref<Context>{new Context}; }

Context::~Context() {
    for (auto &entry : objects_)
        entry.second->context_ = nullptr;
}

void Context::absorb(TXContext &&tx) {
    std::scoped_lock const lock(mutex_);
    for (auto const &entry : tx.objects_)
        if (objects_.contains(entry.first))
            throw kira::Anyhow("Context: object ID is already registered");

    // Validate every map before merging any, so a failure leaves them unchanged.
    for (std::size_t kind = 0; kind < numIndexedKinds; ++kind)
        indices_[kind].validateMerge(tx.indices_[kind]);
    objects_.reserve(objects_.size() + tx.objects_.size());

    // Stage the added IDs and reserve room for them while allocation may still throw.
    std::unordered_set<std::size_t> added;
    for (auto const &entry : tx.objects_)
        added.insert(entry.first);
    addedIds_.reserve(addedIds_.size() + added.size());

    // Merge the validated maps, objects, and IDs. The objects join the scene here.
    for (std::size_t kind = 0; kind < numIndexedKinds; ++kind)
        indices_[kind].mergeValidated(std::move(tx.indices_[kind]));
    for (auto const &entry : tx.objects_)
        entry.second->context_ = this;
    objects_.merge(tx.objects_);
    addedIds_.merge(added);
}

void Context::remove(std::size_t contextId) {
    std::scoped_lock const lock(mutex_);
    auto const iterator = objects_.find(contextId);
    if (iterator == objects_.end())
        throw std::out_of_range("Context: object ID is out of range");
    if (!iterator->second->isRoot())
        throw kira::Anyhow("Context: only a root object can be removed");

    // Find the active roles before the object can be destroyed.
    auto const isIntegrator = activeIntegrator_ && activeIntegrator_->getContextId() == contextId;
    auto const isSampler = activeSampler_ && activeSampler_->getContextId() == contextId;
    auto const isEnvMap = activeEnvMap_ && activeEnvMap_->getContextId() == contextId;
    reclaim(contextId);

    // Empty the active slots that held it. This cannot throw.
    if (isIntegrator) {
        activeIntegrator_.reset();
        dirtyBits_ = dirtyBits_ | DirtyBits::ActiveIntegrator;
    }
    if (isSampler) {
        activeSampler_.reset();
        dirtyBits_ = dirtyBits_ | DirtyBits::ActiveSampler;
    }
    if (isEnvMap) {
        activeEnvMap_.reset();
        dirtyBits_ = dirtyBits_ | DirtyBits::ActiveEnvMap;
    }
}

void Context::reclaim(std::size_t contextId) {
    auto const iterator = objects_.find(contextId);

    // Record the removal first, which can throw.
    removedIds_.insert(contextId);

    // Unlist the object, so the removed set is its only record this epoch. Its index entry stays
    // until \c clearDirty.
    auto &object = *iterator->second;
    changedIds_.erase(contextId);
    addedIds_.erase(contextId);
    object.dirtyMask_ = 0;
    object.context_ = nullptr;

    // Drop the context's reference last, which may destroy the object and release its references.
    objects_.erase(iterator);
}

void Context::collectGarbage() {
    std::vector<std::size_t> garbage;
    do {
        garbage.clear();
        for (auto const &[contextId, object] : objects_)
            if (!object->isRoot() && object->getRefCount() == 1)
                garbage.push_back(contextId);

        // Sort by ID, because the hash map's iteration order is unspecified.
        std::ranges::sort(garbage);

        // Removing an object releases its references, so the next round sees the result.
        for (auto const contextId : garbage)
            reclaim(contextId);
    } while (!garbage.empty());
}

void Context::setActiveIntegrator(Ref<PathIntegrator const> integrator) {
    if (integrator && integrator->getContext() != this)
        throw kira::Anyhow("Context: integrator belongs to another context");

    std::scoped_lock const lock(mutex_);
    if (activeIntegrator_ == integrator)
        return;
    activeIntegrator_ = std::move(integrator);
    dirtyBits_ = dirtyBits_ | DirtyBits::ActiveIntegrator;
}

void Context::setActiveSampler(Ref<Sampler const> sampler) {
    if (sampler && sampler->getContext() != this)
        throw kira::Anyhow("Context: sampler belongs to another context");

    std::scoped_lock const lock(mutex_);
    if (activeSampler_ == sampler)
        return;
    activeSampler_ = std::move(sampler);
    dirtyBits_ = dirtyBits_ | DirtyBits::ActiveSampler;
}

void Context::setActiveEnvMap(Ref<EnvMapLight const> envMap) {
    if (envMap && envMap->getContext() != this)
        throw kira::Anyhow("Context: environment map belongs to another context");

    std::scoped_lock const lock(mutex_);
    if (activeEnvMap_ == envMap)
        return;
    activeEnvMap_ = std::move(envMap);
    dirtyBits_ = dirtyBits_ | DirtyBits::ActiveEnvMap;
}

Ref<PathIntegrator const> Context::getActiveIntegrator() const { return activeIntegrator_; }

Ref<Sampler const> Context::getActiveSampler() const { return activeSampler_; }

Ref<EnvMapLight const> Context::getActiveEnvMap() const { return activeEnvMap_; }

void Context::clearDirty() {
    // Zero the masks. The sets are exact, so every ID is in the scene.
    for (auto const id : changedIds_)
        objects_.at(id)->dirtyMask_ = 0;

    // Erase the index of each removed object. The object is gone, so find the map that holds its
    // ID. Ascending order keeps the order in which later objects reuse the indices.
    std::vector<std::size_t> removed(removedIds_.begin(), removedIds_.end());
    std::ranges::sort(removed);
    for (auto const id : removed)
        for (auto &indices : indices_)
            if (indices.entries_.contains(id))
                (void)indices.erase(id);

    // Close the epoch.
    changedIds_.clear();
    addedIds_.clear();
    removedIds_.clear();
    dirtyBits_ = DirtyBits::None;
    ++epoch_;
}
} // namespace flux
