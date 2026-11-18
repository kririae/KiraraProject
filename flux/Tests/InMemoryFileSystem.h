#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "flux/Core/FileSystem.h"
#include "flux/Core/ResolvedPath.h"

namespace flux::test {
class InMemoryFileSystem final : public FileSystem {
public:
    void addFile(
        ResolvedPath const &resolvedPath, std::filesystem::path const &path,
        std::uint64_t readLimit = std::numeric_limits<std::uint64_t>::max()
    ) {
        auto stream = std::ifstream{path, std::ios::binary};
        auto const bytes = std::vector<char>{
            std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}
        };
        auto stored = std::make_shared<Stored>();
        stored->bytes.resize(bytes.size());
        std::ranges::transform(bytes, stored->bytes.begin(), [](char byte) {
            return static_cast<std::byte>(byte);
        });
        stored->readLimit = readLimit;
        files_[resolvedPath.getPathString()] = std::move(stored);
    }

    [[nodiscard]] std::unique_ptr<RandomAccessFile>
    open(ResolvedPath const &resolvedPath) const override {
        ++openCount_;
        auto const iterator = files_.find(resolvedPath.getPathString());
        if (iterator == files_.end())
            return nullptr;
        return std::make_unique<File>(iterator->second);
    }

    [[nodiscard]] std::string getExtension(ResolvedPath const &resolvedPath) const override {
        auto const extension = u8string(u8path(resolvedPath.getPathString()).extension());
        return extension.empty() ? extension : extension.substr(1);
    }

    [[nodiscard]] int getOpenCount() const { return openCount_; }

private:
    struct Stored {
        std::vector<std::byte> bytes;
        std::uint64_t readLimit;
    };

    class File final : public RandomAccessFile {
    public:
        explicit File(std::shared_ptr<Stored const> stored) : stored_(std::move(stored)) {}

        File(File const &) = delete;
        File &operator=(File const &) = delete;

        [[nodiscard]] std::uint64_t getSize() const noexcept override {
            return stored_->bytes.size();
        }

        [[nodiscard]] std::size_t
        read(std::span<std::byte> buffer, std::uint64_t offset) const noexcept override {
            auto const bytes = std::span{stored_->bytes};
            auto const end = std::min<std::uint64_t>(bytes.size(), stored_->readLimit);
            if (offset >= end)
                return 0;
            auto const count = std::min<std::size_t>(buffer.size(), end - offset);
            std::ranges::copy(bytes.subspan(offset, count), buffer.begin());
            return count;
        }

    private:
        std::shared_ptr<Stored const> stored_;
    };

    std::map<std::string, std::shared_ptr<Stored const>> files_;
    mutable std::atomic_int openCount_{0};
};
} // namespace flux::test
