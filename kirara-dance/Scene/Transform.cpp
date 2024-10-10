#include "Scene/Transform.h"

namespace krd {
float4x4 Transform::getMatrix() const {
    // SRT order
    return cmul(
        cmul(scaling_matrix(scaling), rotation_matrix(rotation)), translation_matrix(translation)
    );
}
} // namespace krd
