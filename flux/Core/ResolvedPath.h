#pragma once

#include <filesystem>
#include <string>
#include <utility>

namespace flux {
[[nodiscard]] inline std::filesystem::path u8path(std::string const &name) {
    return std::filesystem::path{std::u8string{name.begin(), name.end()}};
}

[[nodiscard]] inline std::string u8string(std::filesystem::path const &path) {
    auto const name = path.u8string();
    return std::string{name.begin(), name.end()};
}

/// \brief A UTF-8 path that a file system opens as it is.
class ResolvedPath {
public:
    ResolvedPath() = default;
    explicit ResolvedPath(std::string pathString) : pathString_(std::move(pathString)) {}

    [[nodiscard]] std::string const &getPathString() const noexcept { return pathString_; }
    [[nodiscard]] bool isEmpty() const noexcept { return pathString_.empty(); }

    [[nodiscard]] friend bool operator==(ResolvedPath const &, ResolvedPath const &) = default;

private:
    std::string pathString_;
};

[[nodiscard]] inline std::string
createIdentifier(std::string const &assetPath, ResolvedPath const &anchor) {
    if (assetPath.empty())
        return assetPath;
    return u8string(u8path(anchor.getPathString()).parent_path() / u8path(assetPath));
}
} // namespace flux
