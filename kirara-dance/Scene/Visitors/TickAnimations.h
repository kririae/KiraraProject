#pragma once

#include "Scene/Animation.h"
#include "SceneGraph/Visitors.h"

namespace krd {
/// \brief A visitor that advances animations in the scene graph by a given time step.
///
/// This class traverses the scene graph and calls the `tick` method on any
/// Animation nodes it encounters, effectively progressing the animation state.
class TickAnimations : public Visitor {
public:
    /// \brief A constant representing all animations.
    static constexpr uint64_t ALL_ANIMATIONS = std::numeric_limits<uint64_t>::max();

    struct Desc {
        /// \brief The animation ID to be ticked.
        ///
        /// This ID is used to identify which animation should be advanced.
        /// When no ID is matched, the animation will not be ticked.
        uint64_t animId{ALL_ANIMATIONS};

        /// \brief The time step for the animations to proceed.
        ///
        /// This is the time interval, in seconds, by which to advance the
        /// animations.
        float deltaTime{0};
    };

    /// \brief Constructs a TickAnimations visitor.
    ///
    /// \param deltaTime The time interval, in seconds, by which to advance
    ///                  the animations.
    explicit TickAnimations(Desc const &desc) : desc(desc) {}

    /// \brief Creates a new TickAnimations visitor instance.
    ///
    /// This is a factory method for creating a TickAnimations object wrapped
    /// in a Ref smart pointer.
    /// \param deltaTime The time step for the animations to proceed.
    /// \return A Ref-counted pointer to the new TickAnimations instance.
    [[nodiscard]] static Ref<TickAnimations> create(Desc const &desc) {
        return {new TickAnimations(desc)};
    }

public:
    /// \brief Applies the animation tick to a generic Node.
    ///
    /// This method continues traversal of the scene graph.
    /// \param val The Node to visit.
    void apply(Node &val) override { traverse(val); }

    /// \brief Applies the animation tick to an Animation node.
    ///
    /// This method calls the `tick` method on the Animation object,
    /// advancing its state by the `deltaTime` specified during construction.
    /// \param val The Animation node to tick.
    void apply(Animation &val) override {
        if (desc.animId == ALL_ANIMATIONS || val.getId() == desc.animId) {
            applied = true;
            val.tick(desc.deltaTime);
        }
    }

public:
    /// Returns whether any animations were matched and ticked.
    bool isMatched() const { return applied; }

private:
    Desc desc;
    bool applied{false};
};
} // namespace krd
