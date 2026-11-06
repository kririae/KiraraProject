#include "flux/Optix/OptixShaderPool.h"

#include <utility>

namespace flux {
void OptixShaderPool::add(std::span<Ref<ShaderAsset const> const> assets) {
    if (assets.empty())
        return;

    if (!compiler_)
        compiler_.emplace();

    std::vector<std::shared_ptr<OptixShaderModule const>> modules;
    modules.reserve(assets.size());
    for (auto const &asset : assets) {
        auto const ir = compiler_->compile(asset->getSpec().source);
        modules.push_back(std::make_shared<OptixShaderModule const>(deviceContext_, ir));
    }

    modules_.reserve(modules_.size() + modules.size());
    for (std::size_t i = 0; i < modules.size(); ++i)
        modules_.try_emplace(assets[i]->getId(), std::move(modules[i]));
}

void OptixShaderPool::remove(std::span<std::size_t const> ids) {
    for (auto const id : ids)
        modules_.erase(id);
}

std::vector<std::size_t> OptixShaderPool::getIds() const {
    std::vector<std::size_t> ids;
    ids.reserve(modules_.size());
    for (auto const &[id, module] : modules_)
        ids.push_back(id);
    return ids;
}
} // namespace flux
