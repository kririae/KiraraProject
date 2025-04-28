#pragma once

#include <string>
#include <vector>

#include "Scene2/Geometry.h"
#include "SceneGraph/Node.h"

namespace krd {
class TreeInfo final : public ConstVisitor {
public:
    /// Create a new TreeInfo visitor.
    ///
    /// \remark TreeInfo can be created on stack or heap.
    TreeInfo() = default;
    [[nodiscard]] static Ref<TreeInfo> create() { return {new TreeInfo}; }
    ~TreeInfo() override = default;

public:
    void apply(Node const &t) override {
        if (isLastAtLevel.empty())
            LogInfo(".");
        else
            LogInfo("{}{} ({})", getPrefix(), t.getTypeName(), t.getId());

        auto children = t.traverse(*this);
        std::vector<Ref<Node>> childVec;
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
            LogInfo("{}{} ({})", getPrefix(), t.getTypeName(), t.getId());

        if (t.getMesh()) {
            std::string prefix = getChildPrefix();

            LogInfo("{}├── Vertices: {}", prefix, t.getMesh()->getNumVertices());
            LogInfo("{}└── Faces: {}", prefix, t.getMesh()->getNumFaces());
        }
    }

private:
    std::vector<bool> isLastAtLevel;
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
