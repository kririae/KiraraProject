// Copyright (c) 2017 Wenzel Jakob wenzel.jakob@epfl.ch, All rights reserved.
//
// NOTE(krr): this file is a modified version of mitsuba's src/core/fresolver.cpp
#include "kira/FileResolver.h"

#include <algorithm>

namespace kira {
FileResolver::FileResolver() { paths.push_back(std::filesystem::current_path()); }

FileResolver::FileResolver(FileResolver const &fr) = default;

void FileResolver::erase(std::filesystem::path const &p) {
    paths.erase(std::remove(paths.begin(), paths.end(), p), paths.end());
}

bool FileResolver::contains(std::filesystem::path const &p) const {
    return std::find(paths.begin(), paths.end(), p) != paths.end();
}

std::filesystem::path FileResolver::resolve(std::filesystem::path const &path) const {
    if (!path.is_absolute()) {
        for (auto const &base : paths) {
            std::filesystem::path combined = base / path;
            if (std::filesystem::exists(combined))
                return combined;
        }
    }
    return path;
}
} // namespace kira
