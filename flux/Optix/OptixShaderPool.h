#pragma once

#include <optix_types.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "flux/Core/Object.h"
#include "flux/Optix/OptixBSDFCompiler.h"
#include "flux/Optix/OptixShaderModule.h"
#include "flux/Scene/ShaderAsset.h"

namespace flux {
/// \brief OptiX modules of shader assets, by ShaderAsset ID.
///
/// The pool holds no ShaderAsset, and knows no BSDF. It compiles with one OptixBSDFCompiler, which
/// it creates when it first compiles. Mutation is single-threaded, so callers provide external
/// synchronization.
class OptixShaderPool final : private Noncopyable {
public:
    /// \brief Creates an empty pool whose modules borrow \p deviceContext, which must outlive it.
    explicit OptixShaderPool(OptixDeviceContext deviceContext) noexcept
        : deviceContext_(deviceContext) {}

    /// \brief Compiles the sources of \p assets and builds their modules.
    /// \pre No asset of \p assets is present, and their IDs are distinct.
    /// \throw kira::Anyhow If compiling or building fails. Then nothing is added.
    void add(std::span<Ref<ShaderAsset const> const> assets);

    /// \brief Drops the modules of \p ids from the pool.
    ///
    /// A module that a caller of \c getModule still holds lives on.
    /// \param ids The IDs of the assets. An absent ID is ignored.
    void remove(std::span<std::size_t const> ids);

    /// \brief Returns whether the module of the asset \p id is present.
    [[nodiscard]] bool contains(std::size_t id) const noexcept { return modules_.contains(id); }

    /// \brief Returns the ID of every present asset, in no particular order.
    [[nodiscard]] std::vector<std::size_t> getIds() const;

    /// \brief Returns the module of the asset \p id.
    /// \throw std::out_of_range If the module is absent.
    [[nodiscard]] std::shared_ptr<OptixShaderModule const> getModule(std::size_t id) const {
        return modules_.at(id);
    }

private:
    OptixDeviceContext deviceContext_;
    std::optional<OptixBSDFCompiler> compiler_;
    std::unordered_map<std::size_t, std::shared_ptr<OptixShaderModule const>> modules_;
};
} // namespace flux
