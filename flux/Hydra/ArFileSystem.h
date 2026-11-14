#pragma once

#include <memory>
#include <string>

#include "flux/Core/FileSystem.h"
#include "flux/Core/ResolvedPath.h"

class HdFluxArFileSystem final : public flux::FileSystem {
public:
    [[nodiscard]] std::unique_ptr<flux::RandomAccessFile>
    open(flux::ResolvedPath const &resolvedPath) const override;

    [[nodiscard]] std::string getExtension(flux::ResolvedPath const &resolvedPath) const override;
};
