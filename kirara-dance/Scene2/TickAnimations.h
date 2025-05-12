#pragma once

#include "Scene2/Animation.h"
#include "SceneGraph/Visitors.h"

namespace krd {
/// \brief A visitor that advances animations in the scene graph by a given time step.
///
/// This class traverses the scene graph and calls the `tick` method on any
/// Animation nodes it encounters, effectively progressing the animation state.
class TickAnimations : public Visitor {
public:
    /// \brief Constructs a TickAnimations visitor.
    ///
    /// \param deltaTime The time interval, in seconds, by which to advance
    ///                  the animations.
    explicit TickAnimations(float deltaTime) : deltaTime(deltaTime) {}

    /// \brief Creates a new TickAnimations visitor instance.
    ///
    /// This is a factory method for creating a TickAnimations object wrapped
    /// in a Ref smart pointer.
    /// \param deltaTime The time step for the animations to proceed.
    /// \return A Ref-counted pointer to the new TickAnimations instance.
    [[nodiscard]] static Ref<TickAnimations> create(float deltaTime) {
        return {new TickAnimations(deltaTime)};
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
    void apply(Animation &val) override { val.tick(deltaTime); }

private:
    /// \brief The time step, in seconds, by which animations are advanced.
    float deltaTime{0};
};
} // namespace krd
