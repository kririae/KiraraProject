#pragma once

#include <range/v3/view/single.hpp>

#include "Core/KIRA.h"
#include "Core/Math.h"
#include "SceneGraph/Group.h"
#include "SceneGraph/NodeMixin.h"

namespace krd {
/// Defines how an animation behaves outside the defined time range.
enum class AnimationBehaviour : uint8_t {
    Default = 0, /// Default transformation of the node.
    Constant,    /// The nearest key is used.
    Linear,      /// The adjacent keys are linearly extrapolated.
    Repeat,      /// The animation is repeated.
};

enum class AnimationInterpolation : uint8_t {
    Step,            /// Step interpolation.
    Linear,          /// Linear interpolation.
    SphericalLinear, /// Spherical linear interpolation.
    CubicSpline,     /// Cubic spline interpolation.
};

template <typename T> struct AnimationKey {
    double time;
    T value;
    AnimationInterpolation interp;

    /// Compare two keys by time.
    [[nodiscard]] bool operator<(AnimationKey const &other) const { return time < other.time; }
};

template <typename T> struct AnimationSequence : kira::SmallVector<AnimationKey<T>> {
    /// Get the start time of the sequence.
    ///
    /// \remark The sequence must be sorted to invoke this function.
    [[nodiscard]] float getStartTime() const { return this->empty() ? 0 : this->front().time; }

    /// Get the end time of the sequence.
    ///
    /// \remark The sequence must be sorted to invoke this function.
    [[nodiscard]] float getEndTime() const { return this->empty() ? 0 : this->back().time; }

    /// Get the value at a specific time.
    ///
    /// \tparam useQuaternion Whether to use quaternion interpolation/extrapolation.
    ///
    /// \param time The time to get the value at.
    /// \param preState The behavior before the first key.
    /// \param postState The behavior after the last key.
    /// \param defVal The default value if no keys are present.
    ///
    /// \return The interpolated value at the specified time.
    template <bool useQuaternion = false>
    [[nodiscard]] T getAtTime(
        float time, AnimationBehaviour const preState, AnimationBehaviour const postState,
        T const &defVal
    ) const {
        auto positiveMod = [](auto a, auto b) { return std::fmod(std::fmod(a, b) + b, b); };

        if (this->empty())
            return defVal;

        auto const *it =
            std::upper_bound(this->begin(), this->end(), time, [](float time, auto &key) {
            return time < key.time;
        });

        if (it == this->begin()) {
            switch (preState) {
            case AnimationBehaviour::Default: return defVal;
            case AnimationBehaviour::Constant:
            case AnimationBehaviour::Linear: return this->front().value;
            case AnimationBehaviour::Repeat:
                if (this->size() == 1)
                    return this->front().value;
                auto const rTime =
                    positiveMod(time - getStartTime(), getEndTime() - getStartTime());
                KRD_ASSERT(rTime >= 0, "Animation: rTime should be positive");
                return getAtTime<useQuaternion>(
                    rTime + getStartTime(), preState, postState, defVal
                );
            }
        }

        if (it == this->end()) {
            if (this->empty())
                return defVal;
            switch (postState) {
            case AnimationBehaviour::Default: return defVal;
            case AnimationBehaviour::Constant:
            case AnimationBehaviour::Linear: return this->back().value;
            case AnimationBehaviour::Repeat:
                if (this->size() == 1)
                    return this->back().value;
                auto const rTime =
                    positiveMod(time - getStartTime(), getEndTime() - getStartTime());
                KRD_ASSERT(rTime >= 0, "Animation: rTime should be positive");
                return getAtTime<useQuaternion>(
                    rTime + getStartTime(), preState, postState, defVal
                );
            }
        }

        KRD_ASSERT(this->size() > 1, "Animation: The sequence should have at least 2 keys");
        auto const &prev = *(it - 1);
        auto const &next = *it;

        // Protect duplicated keys.
        if (std::abs(next.time - prev.time) < 1e-5f)
            return prev.value;

        // TODO(krr): Implement SphericalLinear and CubicSpline interpolations.
        switch (prev.interp) {
        case AnimationInterpolation::Step: return prev.value;
        case AnimationInterpolation::Linear:
        case AnimationInterpolation::SphericalLinear:
        case AnimationInterpolation::CubicSpline: {
            auto const t = (time - prev.time) / (next.time - prev.time);
            if constexpr (useQuaternion) {
                // SphericalLinear should use qslerp, Linear with quaternions might also use qslerp
                // or a normalized lerp depending on desired behavior.
                // For now, if useQuaternion is true, we use qslerp for all these cases.
                return qslerp(prev.value, next.value, static_cast<float>(t));
            } else
                return lerp(prev.value, next.value, static_cast<float>(t));
        }
        default: return prev.value;
        }
    }
};

class TransformAnimationChannel final : public NodeMixin<TransformAnimationChannel, Group> {
public:
    using TranslationSeq = AnimationSequence<float3>;
    using RotationSeq = AnimationSequence<float4>;
    using ScalingSeq = AnimationSequence<float3>;

protected:
    ///
    TransformAnimationChannel() = default;

public:
    /// Create an animation.
    ///
    /// \remark The created animation isn't usable until,
    /// - the scene node is bound by \c bindSceneNode,
    /// - all corresponding keys are set by \c setTranslationSeq, \c setRotationSeq, and \c
    /// setScalingSeq.
    /// - The keys are sorted by \c sortKeys.
    static Ref<TransformAnimationChannel> create() { return {new TransformAnimationChannel}; }

    ///
    ~TransformAnimationChannel() override = default;

    ///
    void bindTransform(Ref<Transform> inTransform) { this->transform = std::move(inTransform); }
    ///
    Ref<Transform> getTransform() const { return transform; }
    ///
    void unbindTransform() { transform.reset(); }

    ///
    void doAnim(float curTime);

    /// Sort all sequences in-place by time.
    void sortSeq();

    //
    // A lot of getters and setters. Keep the members private at this time, as modifications to them
    // might be multithreaded.
    //

    [[nodiscard]] TranslationSeq &getTranslationSeq() { return translationSeq; }
    [[nodiscard]] TranslationSeq const &getTranslationSeq() const { return translationSeq; }
    void setTranslationSeq(TranslationSeq inTranslationSeq) {
        translationSeq = std::move(inTranslationSeq);
    }

    [[nodiscard]] RotationSeq &getRotationSeq() { return rotationSeq; }
    [[nodiscard]] RotationSeq const &getRotationSeq() const { return rotationSeq; }
    void setRotationSeq(RotationSeq inRotationSeq) { rotationSeq = std::move(inRotationSeq); }

    [[nodiscard]] ScalingSeq &getScalingSeq() { return scalingSeq; }
    [[nodiscard]] ScalingSeq const &getScalingSeq() const { return scalingSeq; }
    void setScalingSeq(ScalingSeq inScalingSeq) { scalingSeq = std::move(inScalingSeq); }

    [[nodiscard]] AnimationBehaviour getPreState() const { return preState; }
    void setPreState(AnimationBehaviour const &inPreState) { preState = inPreState; }
    [[nodiscard]] AnimationBehaviour getPostState() const { return postState; }
    void setPostState(AnimationBehaviour const &inPostState) { postState = inPostState; }

private:
    /// The scene node to modify.
    Ref<Transform> transform;

    TranslationSeq translationSeq;
    RotationSeq rotationSeq;
    ScalingSeq scalingSeq;

    /// How the animation behaves before the first key.
    AnimationBehaviour preState{AnimationBehaviour::Default};
    /// How the animation behaves after the last key.
    AnimationBehaviour postState{AnimationBehaviour::Default};
};

class Animation final : public NodeMixin<Animation, Group> {
protected:
    Animation() = default;

public:
    ///
    static Ref<Animation> create() { return {new Animation}; }

public:
    void tick(float deltaTime);

private:
#if 0
    /// The duration of the animation.
    double duration{};
    /// The number of ticks per second.
    double ticksPerSecond{};
#endif

    /// The current time of the animation.
    float curTime{0};
};
} // namespace krd
