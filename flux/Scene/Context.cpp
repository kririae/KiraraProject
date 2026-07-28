#include "flux/Scene/Context.h"

#include "flux/Integrator/PathIntegrator.h"
#include "flux/Sampling/Sampler.h"
#include "flux/Scene/RenderObject.h"

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

    objects_.reserve(objects_.size() + tx.objects_.size());
    stagedForLink_.reserve(stagedForLink_.size() + tx.stagedForLink_.size());

    objects_.merge(tx.objects_);
    stagedForLink_.append(tx.stagedForLink_.begin(), tx.stagedForLink_.end());
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

void Context::commit() {
    if (committing_)
        throw kira::Anyhow("Context: recursive commit is not allowed");

    committing_ = true;
    try {
        for (auto const contextId : stagedForLink_)
            get<RenderObject>(contextId)->link();
    } catch (...) {
        committing_ = false;
        throw;
    }

    committing_ = false;
    stagedForLink_.clear();
}
} // namespace flux
