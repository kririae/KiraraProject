#pragma once

#include "Core/KIRA.h"
#include "Core/Object.h"

namespace krd {
class Scene;

/// The facade to construct the scene through different configurations, e.g.,
/// raw TOML, glTF, etc.
///
/// This is only used to bootstrap the scene, later modifications should be done right on the scene.
class SceneBuilder {
public:
    ///
    SceneBuilder() = default;

    ///
    ~SceneBuilder() = default;

public:
    /// Load the scene from a file.
    ///
    /// \throw kira::Anyhow if the file is not found or the file is not a valid scene configuration.
    /// \throw std::exception if any other error occurs.
    void loadFromFile(std::filesystem::path const &path);

public:
    /// Build the scene.
    [[nodiscard]] Ref<Scene> build() const;
};
} // namespace krd
