#pragma once

#include "flux/Render/Film.h"
#include "flux/Scene/RenderObject.h"

namespace flux {
/// \brief Persistent render target selected for a launch.
///
/// The context ID provides stable identity for backend-owned target runtime.
/// Camera selection remains launch-scoped and is not stored here.
///
/// \par Properties
/// - \c width: required nonzero uint32 image width.
/// - \c height: required nonzero uint32 image height.
class RenderProduct final : public RenderObject {
    friend class TXContext;

public:
    /// \brief Returns the channelized film descriptor.
    [[nodiscard]] Film const &getFilm() const noexcept { return film_; }

    /// \brief Returns the mutable channelized film descriptor.
    [[nodiscard]] Film &getFilm() noexcept { return film_; }

private:
    RenderProduct(TXContext &tx, kira::Properties properties);

    Film film_;
};
} // namespace flux
