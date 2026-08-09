#include "flux/Scene/Context.h"

#include "flux/Integrator/PathIntegrator.h"
#include "flux/Sampling/Sampler.h"
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

    imageTextures_.validateMerge(tx.imageTextures_);
    bsdfs_.validateMerge(tx.bsdfs_);
    edfs_.validateMerge(tx.edfs_);
    objects_.reserve(objects_.size() + tx.objects_.size());

    imageTextures_.mergeValidated(std::move(tx.imageTextures_));
    bsdfs_.mergeValidated(std::move(tx.bsdfs_));
    edfs_.mergeValidated(std::move(tx.edfs_));
    objects_.merge(tx.objects_);
    if (!activeIntegratorId_ && tx.activeIntegratorId_)
        activeIntegratorId_ = tx.activeIntegratorId_;
    if (!activeSamplerId_ && tx.activeSamplerId_)
        activeSamplerId_ = tx.activeSamplerId_;
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

void Context::commit() noexcept {}
} // namespace flux
