#include "flux/Scene/ShaderAsset.h"

#include <atomic>
#include <utility>

namespace flux {
namespace {
/// ID of the next ShaderAsset. Assets are created concurrently, so it is atomic.
std::atomic_size_t nextShaderAssetId{0};
} // namespace

ShaderAsset::ShaderAsset(ShaderAssetSpec spec)
    : spec_(std::move(spec)), id_(nextShaderAssetId.fetch_add(1, std::memory_order_relaxed)) {}

Ref<ShaderAsset const> ShaderAssetPool::getOrCreate(ShaderAssetSpec const &spec) {
    return assets_.acquire(spec.source, [&] {
        return Ref<ShaderAsset const>{new ShaderAsset(spec)};
    });
}

void ShaderAssetPool::collectGarbage() {
    // A count of one means the pool holds the only reference: no material and no backend uses it.
    assets_.prune([](Ref<ShaderAsset const> const &asset) { return asset.getRefCount() > 1; });
}
} // namespace flux
