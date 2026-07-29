#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "kira/Properties.h"
#include "kira/Vecteur.h"

namespace flux {
using Vec2f = kira::Vec2f;
using Vec2u = kira::Vecteur<std::uint32_t, 2, kira::defaultBackend>;
using Vec3f = kira::Vec3f;
using Spectrum = Vec3f;
using Vec3u = kira::Vecteur<std::uint32_t, 3, kira::defaultBackend>;
} // namespace flux

namespace kira {
template <typename Scalar, std::size_t Size, vecteur::VecteurBackend Backend>
struct PropertyProcessor<vecteur::Vecteur<Scalar, Size, Backend>> : std::true_type {
    using Vector = vecteur::Vecteur<Scalar, Size, Backend>;

    static constexpr std::string_view name = "kira::Vecteur";

    static toml::array to_toml(Vector const &value) {
        toml::array array;
        for (std::size_t index = 0; index < value.size(); ++index)
            array.push_back(value[index]);
        return array;
    }

    static Vector from_toml(toml::node &node, auto const &) {
        auto const *array = node.as_array();
        if (!array)
            throw Anyhow("Expected an array, but got {}", magic_enum::enum_name(node.type()));
        if constexpr (Size != std::dynamic_extent)
            if (array->size() != Size)
                throw Anyhow("Expected {} elements, but got {}", Size, array->size());

        auto result = [&] {
            if constexpr (Size == std::dynamic_extent)
                return Vector(array->size());
            else
                return Vector{};
        }();
        for (std::size_t index = 0; index < array->size(); ++index) {
            auto const value = array->at(index).template value<Scalar>();
            if (!value)
                throw Anyhow(
                    "Expected a vector element, but got {} at [{}]",
                    magic_enum::enum_name(array->at(index).type()), index
                );
            result[index] = *value;
        }
        return result;
    }
};
} // namespace kira
