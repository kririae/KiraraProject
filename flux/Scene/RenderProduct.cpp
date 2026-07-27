#include "flux/Scene/RenderProduct.h"

#include <utility>

namespace flux {
RenderProduct::RenderProduct(TXContext &tx, kira::Properties properties)
    : RenderObject(tx, std::move(properties)),
      film_(
          getProperties().use<std::uint32_t>("width"), getProperties().use<std::uint32_t>("height")
      ) {}
} // namespace flux
