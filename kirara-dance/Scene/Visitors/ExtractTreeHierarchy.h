#pragma once

#include <ostream>
#include <range/v3/view/filter.hpp>
#include <string>

#include "Scene/Animation.h"
#include "Scene/Geometry.h"
#include "SceneGraph/Node.h"
#include "kira/SmallVector.h"

namespace krd {

enum class NodeDescriptionMode : uint8_t { HumanReadable, TypeName, UUID };

class ExtractTreeHierarchy final : public ConstVisitor {
public:
    struct Descriptor {
        /// The string stream to write to
        std::ostream &os{std::cout};
        /// How to display node description
        NodeDescriptionMode descriptionMode{NodeDescriptionMode::HumanReadable};
    };

    /// Create a new ExtractTreeHierarchy visitor.
    ///
    /// \remark ExtractTreeHierarchy can be created on stack or heap.
    ExtractTreeHierarchy(Descriptor const &desc)
        : out(desc.os), descriptionMode(desc.descriptionMode) {}
    [[nodiscard]] static Ref<ExtractTreeHierarchy> create(Descriptor const &desc) {
        return {new ExtractTreeHierarchy(desc)};
    }

public:
    /// Clear the internal state.
    void clear() { isLastAtLevel.clear(); }

    void apply(Node const &t) override {
        if (isLastAtLevel.empty())
            out << ".\n";
        else
            printNodeDescription(getPrefix(), getNodeDescription(t));

        auto children = t.traverse(*this);
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
                out << fmt::format(
                    "{}Recursive NodeID encountered: {}\n", getPrefix(), child->getId()
                );
            isLastAtLevel.pop_back();
        }
    }

    void apply(Geometry const &t) override {
        if (isLastAtLevel.empty())
            out << ".\n";
        else
            printNodeDescription(getPrefix(), getNodeDescription(t));
    }

    void apply(TransformAnimationChannel const &t) override {
        if (isLastAtLevel.empty())
            out << ".\n";
        else
            printNodeDescription(getPrefix(), getNodeDescription(t));
    }

private:
    kira::SmallVector<bool> isLastAtLevel;
    std::ostream &out;
    NodeDescriptionMode descriptionMode;

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

    std::string getNodeDescription(IsNode auto const &node) const {
        switch (descriptionMode) {
        case NodeDescriptionMode::TypeName: return node.getTypeName();
        case NodeDescriptionMode::UUID: return uuids::to_string(node.getUUID());
        case NodeDescriptionMode::HumanReadable:
        default: return node.getHumanReadable();
        }
    }

    // 新增：支持多行描述的输出
    void printNodeDescription(const std::string& prefix, const std::string& desc) {
        std::istringstream iss(desc);
        std::string line;
        bool first = true;
        while (std::getline(iss, line)) {
            if (!first)
                out << prefix; // 除首行外都补前缀
            out << line << '\n';
            first = false;
        }
    }
};
} // namespace krd
