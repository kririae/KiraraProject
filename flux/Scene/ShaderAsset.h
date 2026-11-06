#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace flux {
/// \brief One input of a shader, and where its value lives.
struct ShaderParameter {
    /// \brief The path of the input inside the material, such as \c oren/color.
    std::string path;

    /// \brief The type of the value, such as \c color3.
    std::string type;

    std::size_t wordCount{};

    /// \brief The index of its first word.
    std::size_t offset{};
};

/// \brief The generated Slang of a shader, and where its parameters live.
struct ShaderAssetSpec {
    /// \brief The Slang module that declares \c makeBSDF.
    std::string source;

    std::vector<ShaderParameter> parameters;

    [[nodiscard]] friend bool
    operator==(ShaderAssetSpec const &, ShaderAssetSpec const &) = default;
};
} // namespace flux
