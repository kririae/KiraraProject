#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "flux/Core/ResolvedPath.h"

namespace flux {
/// \brief A read-only file of fixed size, read at an offset, from several threads at once.
class RandomAccessFile {
public:
    virtual ~RandomAccessFile() = default;

    [[nodiscard]] virtual std::uint64_t getSize() const noexcept = 0;

    /// \brief Reads the bytes at an offset into a buffer, and returns the count read.
    ///
    /// The count is less than the size of the buffer only at the end of the file or on a failure.
    [[nodiscard]] virtual std::size_t
    read(std::span<std::byte> buffer, std::uint64_t offset) const noexcept = 0;
};

/// \brief Opens resolved paths, from several threads at once.
///
/// A memory failure throws.
class FileSystem {
public:
    virtual ~FileSystem() = default;

    /// \brief Opens a resolved path, or returns null if this file system cannot open it.
    [[nodiscard]] virtual std::unique_ptr<RandomAccessFile>
    open(ResolvedPath const &resolvedPath) const = 0;

    /// \brief Returns the extension of a resolved path, without the dot.
    [[nodiscard]] virtual std::string getExtension(ResolvedPath const &resolvedPath) const = 0;
};
} // namespace flux
