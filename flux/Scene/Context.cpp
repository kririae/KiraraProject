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
    for (auto const &entry : tx.objects_)
        if (objects_.contains(entry.first))
            throw kira::Anyhow("Context: object ID is already registered");

    // Validate every map before merging any, so a failure leaves them unchanged.
    for (std::size_t kind = 0; kind < numIndexedKinds; ++kind)
        indices_[kind].validateMerge(tx.indices_[kind]);
    objects_.reserve(objects_.size() + tx.objects_.size());

    // List the added IDs while allocation may still throw.
    addedIds_.reserve(addedIds_.size() + tx.objects_.size());
    auto const firstAdded = addedIds_.size();
    for (auto const &entry : tx.objects_)
        addedIds_.push_back(entry.first);
    std::sort(addedIds_.begin() + static_cast<std::ptrdiff_t>(firstAdded), addedIds_.end());
    if (!tx.objects_.empty())
        dirtyBits_ = dirtyBits_ | DirtyBits::Added;

    // Merge the validated maps and objects.
    for (std::size_t kind = 0; kind < numIndexedKinds; ++kind)
        indices_[kind].mergeValidated(std::move(tx.indices_[kind]));
    objects_.merge(tx.objects_);

    // The first object of each active kind becomes active.
    if (!activeIntegratorId_)
        setActive(activeIntegratorId_, tx.activeIntegratorId_, DirtyBits::ActiveIntegrator);
    if (!activeSamplerId_)
        setActive(activeSamplerId_, tx.activeSamplerId_, DirtyBits::ActiveSampler);
    if (!activeEnvMapId_)
        setActive(activeEnvMapId_, tx.activeEnvMapId_, DirtyBits::ActiveEnvMap);
}

void Context::setActive(
    std::optional<std::size_t> &slot, std::optional<std::size_t> id, DirtyBits bits
) {
    if (slot == id)
        return;
    slot = id;
    dirtyBits_ = dirtyBits_ | bits;
}

void Context::setActiveIntegrator(Ref<PathIntegrator const> integrator) {
    if (!integrator || integrator->getContext() != this)
        throw kira::Anyhow("Context: integrator is null or belongs to another context");
    setActive(activeIntegratorId_, integrator->getContextId(), DirtyBits::ActiveIntegrator);
}

void Context::setActiveSampler(Ref<Sampler const> sampler) {
    if (!sampler || sampler->getContext() != this)
        throw kira::Anyhow("Context: sampler is null or belongs to another context");
    setActive(activeSamplerId_, sampler->getContextId(), DirtyBits::ActiveSampler);
}

void Context::setActiveEnvMap(Ref<EnvMapLight const> envMap) {
    if (envMap && envMap->getContext() != this)
        throw kira::Anyhow("Context: environment map belongs to another context");
    setActive(
        activeEnvMapId_, envMap ? std::optional{envMap->getContextId()} : std::nullopt,
        DirtyBits::ActiveEnvMap
    );
}

Ref<PathIntegrator const> Context::getActiveIntegrator() const {
    if (!activeIntegratorId_)
        throw kira::Anyhow("Context: no active integrator is set");
    return get<PathIntegrator>(*activeIntegratorId_);
}

Ref<Sampler const> Context::getActiveSampler() const {
    if (!activeSamplerId_)
        throw kira::Anyhow("Context: no active sampler is set");
    return get<Sampler>(*activeSamplerId_);
}

Ref<EnvMapLight const> Context::getActiveEnvMap() const {
    if (!activeEnvMapId_)
        return {};
    return get<EnvMapLight>(*activeEnvMapId_);
}

void Context::commit() noexcept {}

void Context::clearDirty() noexcept {
    for (auto const id : changedIds_)
        objects_.at(id)->dirtyMask_ = 0;
    changedIds_.clear();
    addedIds_.clear();
    dirtyBits_ = DirtyBits::None;
    ++epoch_;
}
} // namespace flux
