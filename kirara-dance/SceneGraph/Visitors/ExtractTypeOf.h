#pragma once

#include "../Visitors.h"

namespace krd {
/// \brief A visitor that extracts all nodes of a specific type T from a scene graph.
///
/// This class traverses a scene graph and collects references to all nodes that are of type T or a
/// subclass of T. The collected nodes are stored in a \c SmallVector.
///
/// \tparam T The type of nodes to extract.
/// \tparam isConst A boolean indicating whether the nodes are const or non-const, defaulting to \c
/// std::is_const_v<T>.
template <typename T, bool isConst = std::is_const_v<T>> class ExtractTypeOf;

///
template <typename T>
class ExtractTypeOf<T, true> : public ConstVisitor, public kira::SmallVector<Ref<T>> {
public:
    [[nodiscard]] static Ref<ExtractTypeOf> create() { return {new ExtractTypeOf}; }

public:
    void apply(Node const &val) override { this->traverse(val); }
    void apply(T &val) override {
        this->emplace_back(&val);
        this->traverse(val);
    }
};

///
template <typename T>
class ExtractTypeOf<T, false> : public Visitor, public kira::SmallVector<Ref<T>> {
public:
    [[nodiscard]] static Ref<ExtractTypeOf> create() { return {new ExtractTypeOf}; }

public:
    void apply(Node &val) override { this->traverse(val); }
    void apply(T &val) override {
        this->emplace_back(&val);
        this->traverse(val);
    }
};
} // namespace krd
