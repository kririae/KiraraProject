#pragma once

#include "Visitors.h"

namespace krd {
template <typename T>
class ExtractTypeOf : public ConstVisitor, public kira::SmallVector<Ref<T const>> {
public:
    [[nodiscard]] static Ref<ExtractTypeOf> create() { return {new ExtractTypeOf}; }

public:
    void apply(Node const &val) override { traverse(val); }
    void apply(T const &val) override {
        this->emplace_back(&val);
        traverse(val);
    }
};
} // namespace krd
