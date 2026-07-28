#include "flux/Scene/Geometry.h"

#include <utility>

namespace flux {
Geometry::Geometry(TXContext &tx, kira::Properties properties, GeometryType type)
    : RenderObject(tx, std::move(properties)), type_(type) {}
} // namespace flux
