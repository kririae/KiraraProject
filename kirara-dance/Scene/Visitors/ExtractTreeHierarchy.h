#pragma once

#include <range/v3/view/filter.hpp>
#include <string>

#include "Scene/Animation.h"
#include "Scene/Geometry.h"
#include "SceneGraph/Node.h"
#include "kira/SmallVector.h"

namespace krd {
class ExtractTreeHierarchy final : public ConstVisitor {
public:
    /// Create a new ExtractTreeHierarchy visitor.
    ///
    /// \remark ExtractTreeHierarchy can be created on stack or heap.
    ExtractTreeHierarchy() = default;
    [[nodiscard]] static Ref<ExtractTreeHierarchy> create() { return {new ExtractTreeHierarchy}; }

public:
    /// Clear the internal state.
    void clear() { isLastAtLevel.clear(); }

    void apply(Node const &t) override {
        if (isLastAtLevel.empty())
            LogInfo(".");
        else
            LogInfo("{}{}", getPrefix(), t.getHumanReadable());

        auto children = t.traverse(*this) |
                        ranges::views::filter([](auto const &child) { return child != nullptr; });
        kira::SmallVector<Ref<Node>> childVec;
        for (auto const &child : children)
            childVec.push_back(child);

        auto const currId = t.getId();

        for (size_t i = 0; i < childVec.size(); ++i) {
            bool isLast = (i == childVec.size() - 1);
            isLastAtLevel.push_back(isLast);

            auto const &child = childVec[i];
            if (child->getId() != currId)
                child->accept(*this);
            else
                LogError("{}Recursive NodeID encountered: {:d}", getPrefix(), child->getId());
            isLastAtLevel.pop_back();
        }
    }

    void apply(Geometry const &t) override {
        if (isLastAtLevel.empty())
            LogInfo(".");
        else
            LogInfo("{}{}", getPrefix(), t.getHumanReadable());

        if (t.getMesh()) {
            std::string prefix = getChildPrefix();

            LogInfo("{}├── Vertices: {}", prefix, t.getMesh()->getNumVertices());
            LogInfo("{}├── Faces: {}", prefix, t.getMesh()->getNumFaces());
            LogInfo("{}└──(weak): {}", prefix, t.getMesh()->getHumanReadable());
        }
    }

    void apply(TransformAnimationChannel const &t) override {
        if (isLastAtLevel.empty())
            LogInfo(".");
        else
            LogInfo("{}{}", getPrefix(), t.getHumanReadable());

        if (t.getTransform()) {
            std::string prefix = getChildPrefix();
            LogInfo("{}└──(weak): {}", prefix, t.getTransform()->getHumanReadable());
        }
    }

private:
    kira::SmallVector<bool> isLastAtLevel;
    std::string getPrefix() const {
        if (isLastAtLevel.empty())
            return "";
        std::string result;
        for (size_t i = 0; i < isLastAtLevel.size() - 1; ++i)
            result += isLastAtLevel[i] ? "    " : "│   ";
        result += isLastAtLevel.back() ? "└── " : "├── ";

        return result;
    }

    std::string getChildPrefix() const {
        std::string result;
        for (auto isLast : isLastAtLevel)
            result += isLast ? "    " : "│   ";
        return result;
    }
};
} // namespace krd
