#include "flux/Sampling/Distribution2D.h"

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

#include "kira/Anyhow.h"

namespace flux {
float buildCDF2D(Vec2u extent, std::span<float> weights, std::span<float> rowCDF) {
    auto const width = static_cast<std::size_t>(extent.x());
    auto const height = static_cast<std::size_t>(extent.y());
    if (width == 0 || height == 0 || width > std::numeric_limits<std::size_t>::max() / height ||
        weights.size() != width * height || rowCDF.size() != height)
        throw kira::Anyhow("buildCDF2D: storage does not match the distribution extent");

    std::vector<double> rowWeights(height);
    tbb::parallel_for(tbb::blocked_range<std::size_t>{0, height}, [&](auto const &range) {
        for (auto rowIndex = range.begin(); rowIndex != range.end(); ++rowIndex) {
            auto row = weights.subspan(rowIndex * width, width);
            double sum = 0.0;
            for (auto &weight : row) {
                weight = weight > 0.0F ? weight : 0.0F;
                sum += weight;
            }

            rowWeights[rowIndex] = sum;
            if (sum > 0.0) {
                double cumulative = 0.0;
                for (auto &weight : row) {
                    cumulative += weight;
                    weight = static_cast<float>(cumulative / sum);
                }
                row.back() = 1.0F;
            } else {
                for (std::size_t column = 0; column < width; ++column)
                    row[column] = static_cast<float>(column + 1) / static_cast<float>(width);
            }
        }
    });

    double sum = 0.0;
    for (auto const rowWeight : rowWeights)
        sum += rowWeight;
    if (sum > 0.0) {
        double cumulative = 0.0;
        for (std::size_t row = 0; row < height; ++row) {
            cumulative += rowWeights[row];
            rowCDF[row] = static_cast<float>(cumulative / sum);
        }
        rowCDF.back() = 1.0F;
    } else {
        for (std::size_t row = 0; row < height; ++row)
            rowCDF[row] = static_cast<float>(row + 1) / static_cast<float>(height);
    }
    return static_cast<float>(
        std::min(sum, static_cast<double>(std::numeric_limits<float>::max()))
    );
}
} // namespace flux
