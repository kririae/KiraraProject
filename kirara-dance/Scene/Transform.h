#pragma once

#include <range/v3/view/single.hpp>

#include "Core/Math.h"
#include "SceneGraph/Group.h"
#include "SceneGraph/NodeMixins.h"

namespace krd {
class Transform : public SerializableMixin<Transform, Group> {
public:
    [[nodiscard]] static Ref<Transform> create() { return {new Transform}; }

    constexpr static auto archive(auto &archive, auto &self) {
        return archive(self.name, self.translation, self.rotation, self.scaling);
    }

public:
    void setName(std::string const &name) { this->name = name; }
    [[nodiscard]] auto const &getName() const { return name; }

    ///
    void setTranslation(float3 const &translation) { this->translation = translation; }
    ///
    [[nodiscard]] auto const &getTranslation() const { return translation; }

    ///
    void setRotation(float4 const &rotation) { this->rotation = rotation; }
    ///
    [[nodiscard]] auto const &getRotation() const { return rotation; }

    ///
    void setScaling(float3 const &scaling) { this->scaling = scaling; }
    ///
    [[nodiscard]] auto const &getScaling() const { return scaling; }

    /// Returns the TRS transformation matrix.
    [[nodiscard]] float4x4 getMatrix() const;

    std::string getHumanReadable() const override {
        if (name.empty())
            return fmt::format("[{} ({})]", getTypeName(), getId());
        return fmt::format("[{} ({}): {}]", getTypeName(), getId(), name);
    }

protected:
    Transform() = default;
#if 0
    ///
    Transform(float3 const &translation, float4 const &rotation, float3 const &scaling)
        : translation(translation), rotation(rotation), scaling(scaling) {}
#endif

private:
    std::string name;

    // By default, TRS order is used.
    float3 translation{0.0f};
    float4 rotation{0.0f, 0.0f, 0.0f, 1.0f}; // quaternion
    float3 scaling{1.0f};

public:
    void archive(auto &ar) {
        ar(cereal::make_nvp("name", name));
        ar(cereal::make_nvp("translation", translation));
        ar(cereal::make_nvp("rotation", rotation));
        ar(cereal::make_nvp("scaling", scaling));
    }
};
} // namespace krd
