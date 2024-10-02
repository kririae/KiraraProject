#pragma once

namespace krd {
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
};
} // namespace krd
