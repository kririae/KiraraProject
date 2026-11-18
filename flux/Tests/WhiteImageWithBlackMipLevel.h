#pragma once

#include <filesystem>

namespace flux::test {
/// \brief Writes an RGB EXR image of 2x2 white texels with a black authored mip level of 1x1.
///
/// A resample of level 0 would make level 1 white.
void writeWhiteImageWithBlackMipLevel(std::filesystem::path const &path);
} // namespace flux::test
