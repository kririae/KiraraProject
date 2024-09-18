#pragma once

namespace krd {
class SlangContext {
public:
    SlangContext();
    ~SlangContext();

protected:
    struct Impl;
    Impl *pImpl{nullptr};
};
} // namespace krd
