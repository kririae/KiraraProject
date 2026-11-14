#pragma once

#include <filesystem>

namespace flux::test {
/// The header opens, and the levels that precede the last one read.
void writeImageWithUnreadableMipLevel(std::filesystem::path const &path);
} // namespace flux::test
