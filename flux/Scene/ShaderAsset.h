#pragma once

#include <cstddef>
#include <string>

#include "flux/Core/ConcurrentPool.h"
#include "flux/Core/Object.h"

namespace flux {
/// \brief What the source of a shader determines.
struct ShaderAssetSpec {
    /// \brief The Slang module that declares \c makeBSDF.
    std::string source;

    /// \brief False only if the source provably gives an EDF that emits nothing, whatever the
    /// words, assuming that every value is finite.
    bool mayEmit{true};
};

/// \brief Generated shader shared by materials.
///
/// A backend can key what it compiles from the asset by its ID.
class ShaderAsset final : public RefCountedBase<ShaderAsset> {
    friend class ShaderAssetPool;

public:
    /// \brief Returns the ID of this asset. IDs only increase, so no other asset shares it.
    [[nodiscard]] std::size_t getId() const noexcept { return id_; }

    [[nodiscard]] ShaderAssetSpec const &getSpec() const noexcept { return spec_; }

private:
    explicit ShaderAsset(ShaderAssetSpec spec);

    ShaderAssetSpec spec_;
    std::size_t id_;
};

/// \brief Shares ShaderAsset objects by source.
///
/// Concurrent calls share one creation for each source.
class ShaderAssetPool {
public:
    /// \brief Returns the shared asset for the source of \p spec.
    [[nodiscard]] Ref<ShaderAsset const> getOrCreate(ShaderAssetSpec const &spec);

    /// \brief Drops every asset that only this pool references.
    ///
    /// \pre No call to \c getOrCreate runs concurrently.
    void collectGarbage();

    /// \brief Returns the number of assets in the pool.
    [[nodiscard]] std::size_t size() const { return assets_.size(); }

private:
    ConcurrentPool<std::string, Ref<ShaderAsset const>> assets_;
};
} // namespace flux
