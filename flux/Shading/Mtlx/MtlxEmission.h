#pragma once

#include <MaterialXGenShader/ShaderGraph.h>

#include <functional>
#include <optional>
#include <set>
#include <string>

namespace flux {
/// \brief The paths of the zero literals that a proof that a surface emits nothing reads.
using ZeroProof = std::set<std::string, std::less<>>;

/// \brief A proof that the surface of \p graph emits nothing, or \c std::nullopt if none is found.
///
/// The proof folds literals and arithmetic to an exact zero. An unknown leaf, such as an image or a
/// geometric node, may be nonzero. So the proof holds for every value of the words of a shader
/// whose source holds each literal of the proof.
[[nodiscard]] std::optional<ZeroProof> findZeroProof(MaterialX::ShaderGraph const &graph);
} // namespace flux
