#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "flux/Core/Math.h"
#include "flux/Core/MathUtils.h"
#include "kira/Compiler.h"

namespace flux {
/// \brief Piecewise-constant distribution over the unit square.
struct Distribution2D {
    /// Row-major conditional CDFs, one normalized CDF per row.
    float const *condCDF{};
    /// Normalized CDF selecting a row.
    float const *rowCDF{};
    Vec2u extent{};

public:
    /// \brief Samples the unit square and returns its area density in \p pdf.
    ///
    /// \pre \c condCDF and \c rowCDF describe \c extent nonempty cells.
    /// \pre \p u is in the half-open unit square.
    [[nodiscard]] KIRA_HOST_DEVICE Vec2f sample(Vec2f u, float &pdf) const noexcept;

    /// \brief Returns the area density at \p uv.
    ///
    /// Returns zero outside the half-open unit square or for an empty view.
    [[nodiscard]] KIRA_HOST_DEVICE float pdf(Vec2f uv) const noexcept;

    [[nodiscard]] KIRA_HOST_DEVICE explicit operator bool() const noexcept {
        return condCDF && rowCDF && extent.x() != 0 && extent.y() != 0;
    }

private:
    [[nodiscard]] KIRA_HOST_DEVICE static float sample1D(
        float const *cdf, std::uint32_t size, float u, std::uint32_t &index, float &pdf
    ) noexcept;

    [[nodiscard]] KIRA_HOST_DEVICE static float
    pdf1D(float const *cdf, std::uint32_t size, std::uint32_t index) noexcept;
};

/// \brief Builds a two-dimensional CDF from nonnegative cell weights.
///
/// \p weights becomes the row-major conditional CDFs. \p rowCDF receives the
/// row CDF. Negative weights become zero. The return value is the bounded
/// weight sum; zero total weight produces a uniform distribution.
[[nodiscard]] float buildCDF2D(Vec2u extent, std::span<float> weights, std::span<float> rowCDF);

static_assert(std::is_standard_layout_v<Distribution2D>);
static_assert(std::is_trivially_copyable_v<Distribution2D>);

KIRA_HOST_DEVICE inline float Distribution2D::sample1D(
    float const *cdf, std::uint32_t size, float u, std::uint32_t &index, float &pdf
) noexcept {
    if (!(u < 1.0F))
        u = std::nextafter(1.0F, 0.0F);
    index = static_cast<std::uint32_t>(
        std::min(upperBoundIndex(cdf, size, u), static_cast<std::size_t>(size - 1))
    );
    auto const prev = index == 0 ? 0.0F : cdf[index - 1];
    auto const mass = cdf[index] - prev;
    if (!(mass > 0.0F)) {
        pdf = 0.0F;
        return (static_cast<float>(index) + 0.5F) / static_cast<float>(size);
    }

    pdf = mass * static_cast<float>(size);
    return (static_cast<float>(index) + (u - prev) / mass) / static_cast<float>(size);
}

KIRA_HOST_DEVICE inline float
Distribution2D::pdf1D(float const *cdf, std::uint32_t size, std::uint32_t index) noexcept {
    auto const prev = index == 0 ? 0.0F : cdf[index - 1];
    return (cdf[index] - prev) * static_cast<float>(size);
}

KIRA_HOST_DEVICE inline Vec2f Distribution2D::sample(Vec2f u, float &pdfValue) const noexcept {
    if (!*this) {
        pdfValue = 0.0F;
        return {};
    }

    std::uint32_t row;
    float rowPDF;
    auto const v = sample1D(rowCDF, extent.y(), u.y(), row, rowPDF);

    std::uint32_t column;
    float condPDF;
    auto const x = sample1D(
        condCDF + static_cast<std::size_t>(row) * extent.x(), extent.x(), u.x(), column, condPDF
    );
    (void)column;
    pdfValue = rowPDF * condPDF;
    return {x, v};
}

KIRA_HOST_DEVICE inline float Distribution2D::pdf(Vec2f uv) const noexcept {
    if (!*this || uv.x() < 0.0F || uv.x() >= 1.0F || uv.y() < 0.0F || uv.y() >= 1.0F)
        return 0.0F;

    auto const column = std::min(
        static_cast<std::uint32_t>(uv.x() * static_cast<float>(extent.x())), extent.x() - 1
    );
    auto const row = std::min(
        static_cast<std::uint32_t>(uv.y() * static_cast<float>(extent.y())), extent.y() - 1
    );
    auto const rowPDF = pdf1D(rowCDF, extent.y(), row);
    auto const *cond = condCDF + static_cast<std::size_t>(row) * extent.x();
    auto const condPDF = pdf1D(cond, extent.x(), column);
    return rowPDF * condPDF;
}
} // namespace flux
