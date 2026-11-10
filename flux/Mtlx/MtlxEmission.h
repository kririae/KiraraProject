#pragma once

#include <MaterialXGenShader/ShaderGraph.h>

#include "flux/Mtlx/FluxShaderGraphRefactor.h"

namespace flux {
/// \brief Returns false only if the surface of \p graph provably emits nothing.
///
/// The proof folds literals and arithmetic to an exact zero. An unknown leaf, such as an image, a
/// geometric node, or an input in \p interactive, may be nonzero. So the answer holds for every
/// value of the words of the shader.
[[nodiscard]] bool
mayEmit(MaterialX::ShaderGraph const &graph, InteractiveInputs const &interactive);
} // namespace flux
