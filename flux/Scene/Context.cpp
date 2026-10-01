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

    // Merge the validated maps and objects.
    for (std::size_t kind = 0; kind < numIndexedKinds; ++kind)
        indices_[kind].mergeValidated(std::move(tx.indices_[kind]));
    objects_.merge(tx.objects_);
    if (!activeIntegratorId_ && tx.activeIntegratorId_)
        activeIntegratorId_ = tx.activeIntegratorId_;
    if (!activeSamplerId_ && tx.activeSamplerId_)
        activeSamplerId_ = tx.activeSamplerId_;
    if (!activeEnvMapId_ && tx.activeEnvMapId_)
        activeEnvMapId_ = tx.activeEnvMapId_;
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
} // namespace flux
