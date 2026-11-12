#include "flux/Hydra/Transform.h"

std::array<float, 12> HdFluxToTransform(pxr::GfMatrix4d const &matrix) {
    std::array<float, 12> transform{};
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 4; ++column)
            transform[4 * row + column] = static_cast<float>(matrix[column][row]);
    return transform;
}
