// Copyright (c) 2017 Wenzel Jakob wenzel.jakob@epfl.ch, All rights reserved.
//
// NOTE(krr): this file is a modified version of mitsuba/core/fresolver.h, with the following
// modifications:
// - fs::path -> std::filesystem::path
// - std::vector -> SmallVector
// - add [[nodiscard]] to suppress warnings
// - remove unnecessary dependencies
#pragma once

#include <filesystem>

#include "kira/SmallVector.h"

namespace kira {
/// \brief Simple class for resolving paths on Linux/Windows/Mac OS
///
/// This convenience class looks for a file or directory given its name and a set of search paths.
/// The implementation walks through the search paths in order and stops once the file is found.
class FileResolver {
public:
    using iterator = SmallVector<std::filesystem::path>::iterator;
    using const_iterator = SmallVector<std::filesystem::path>::const_iterator;

    /// Initialize a new file resolver with the current working directory
    FileResolver();

    /// Copy constructor
    FileResolver(FileResolver const &fr);

    /// Walk through the list of search paths and try to resolve the input path
    [[nodiscard]] std::filesystem::path resolve(std::filesystem::path const &path) const;

    /// Return the number of search paths
    [[nodiscard]] size_t size() const { return paths.size(); }

    /// Return an iterator at the beginning of the list of search paths
    iterator begin() { return paths.begin(); }

    /// Return an iterator at the end of the list of search paths
    iterator end() { return paths.end(); }

    /// Return an iterator at the beginning of the list of search paths (const)
    [[nodiscard]] const_iterator begin() const { return paths.begin(); }

    /// Return an iterator at the end of the list of search paths (const)
    [[nodiscard]] const_iterator end() const { return paths.end(); }

    /// Check if a given path is included in the search path list
    [[nodiscard]] bool contains(std::filesystem::path const &p) const;

    /// Erase the entry at the given iterator position
    void erase(iterator it) { paths.erase(it); }

    /// Erase the search path from the list
    void erase(std::filesystem::path const &p);

    /// Clear the list of search paths
    void clear() { paths.clear(); }

    /// Prepend an entry at the beginning of the list of search paths
    void prepend(std::filesystem::path const &path) { paths.insert(paths.begin(), path); }

    /// Append an entry to the end of the list of search paths
    void append(std::filesystem::path const &path) { paths.push_back(path); }

    /// Return an entry from the list of search paths
    std::filesystem::path &operator[](size_t index) { return paths[index]; }

    /// Return an entry from the list of search paths (const)
    std::filesystem::path const &operator[](size_t index) const { return paths[index]; }

private:
    SmallVector<std::filesystem::path> paths;
};
} // namespace kira
