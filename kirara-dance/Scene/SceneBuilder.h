#pragma once

namespace krd {
class SceneBuilder {
public:
    ///
    SceneBuilder() = default;

    ///
    ~SceneBuilder() = default;

public:
    /// physics scene
    /// renderer scene
};
} // namespace krd

sceneBuilder.register<Camera>(...);
sceneBuilder.register<Animation>(...);

auto cameraHandler = scene->create<Camera>(...);
auto geometryHandler = scene->create<Geometry>(...);
auto materialHandler = scene->create<Material>(...);
