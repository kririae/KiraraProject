#include "flux/Optix/OptixShaderPool.h"

#include <optix_stubs.h>
#include <tbb/parallel_for.h>
#include <tbb/partitioner.h>

#include <utility>

#include "flux/Optix/OptixUtils.h"

namespace flux {
namespace {
[[nodiscard]] unsigned int getDirectStackSize(OptixProgramGroup program) {
    OptixStackSizes stackSizes{};
    optixCheck(optixProgramGroupGetStackSize(program, &stackSizes, nullptr));
    return stackSizes.dssDC;
}
} // namespace

std::vector<OptixShaderPool::ModuleStats>
OptixShaderPool::add(std::span<Ref<ShaderAsset const> const> assets) {
    if (assets.empty())
        return {};

    if (!compiler_)
        compiler_.emplace();

    std::vector<std::shared_ptr<OptixShaderModule const>> modules(assets.size());
    std::vector<ModuleStats> stats(assets.size());
    tbb::parallel_for(std::size_t{0}, assets.size(), [&](std::size_t i) {
        auto compilation = compiler_->compile(assets[i]->getSpec().source);
        auto const start = std::chrono::steady_clock::now();
        modules[i] = std::make_shared<OptixShaderModule const>(deviceContext_, compilation.ir);
        std::chrono::nanoseconds const moduleTime = std::chrono::steady_clock::now() - start;
        stats[i] = {
            .programName = std::move(compilation.programName),
            .slangTime = compilation.slangTime,
            .nvrtcTime = compilation.nvrtcTime,
            .moduleTime = moduleTime,
            .bsdfDirectStackSize = getDirectStackSize(modules[i]->getBSDFProgramGroup()),
            .edfDirectStackSize = getDirectStackSize(modules[i]->getEDFProgramGroup()),
        };
    }, tbb::simple_partitioner{});

    modules_.reserve(modules_.size() + modules.size());
    for (std::size_t i = 0; i < modules.size(); ++i)
        modules_.try_emplace(assets[i]->getId(), std::move(modules[i]));
    return stats;
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
