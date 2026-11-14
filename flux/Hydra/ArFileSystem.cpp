#include "flux/Hydra/ArFileSystem.h"

#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/resolvedPath.h>
#include <pxr/usd/ar/resolver.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {
class ArFile final : public flux::RandomAccessFile {
public:
    explicit ArFile(std::shared_ptr<ArAsset> asset) noexcept : asset_(std::move(asset)) {}

    [[nodiscard]] std::uint64_t getSize() const noexcept override { return asset_->GetSize(); }

    [[nodiscard]] std::size_t
    read(std::span<std::byte> buffer, std::uint64_t offset) const noexcept override {
        auto const size = getSize();
        if (offset >= size)
            return 0;
        auto const count = std::min<std::uint64_t>(buffer.size(), size - offset);
        return asset_->Read(
            buffer.data(), static_cast<std::size_t>(count), static_cast<std::size_t>(offset)
        );
    }

private:
    std::shared_ptr<ArAsset> asset_;
};
} // namespace

std::unique_ptr<flux::RandomAccessFile>
HdFLuxArFileSystem::open(flux::ResolvedPath const &resolvedPath) const {
    auto asset = ArGetResolver().OpenAsset(ArResolvedPath{resolvedPath.getPathString()});
    if (!asset)
        return nullptr;
    return std::make_unique<ArFile>(std::move(asset));
}

std::string HdFLuxArFileSystem::getExtension(flux::ResolvedPath const &resolvedPath) const {
    return ArGetResolver().GetExtension(resolvedPath.getPathString());
}
