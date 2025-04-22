#include "Transform.h"

namespace krd {
float4x4 Transform::getMatrix() const {
    // TRS order: T * (R * S)
    return mul(
        translation_matrix(translation), mul(rotation_matrix(rotation), scaling_matrix(scaling))
    );
}
} // namespace krd
