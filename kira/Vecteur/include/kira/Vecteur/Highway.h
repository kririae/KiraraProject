#pragma once

#include <hwy/highway.h>

#include "kira/Compiler.h"

#include "Base.h"
#include "Traits.h"

namespace kira::vecteur {
namespace hn = hwy::HWY_NAMESPACE;

#if defined(__CUDA_ARCH__)
/// A Fake a non-constexpr base when all implementations are not presented.
template <typename Scalar, std::size_t Size, typename Derived>
struct VecteurImpl<Scalar, Size, VecteurBackend::Generic, false, Derived>
    : VecteurImpl<Scalar, Size, VecteurBackend::Generic, true, Derived> {
    using Base = VecteurImpl<
        Scalar, Size, VecteurBackend::Generic, true,
        Vecteur<Scalar, Size, VecteurBackend::Generic>>;
    using Base::Base;
    using Base::operator=;
};
#else
template <typename Scalar, std::size_t Size, typename Derived>
struct VecteurImpl<Scalar, Size, VecteurBackend::Generic, false, Derived>
    : VecteurImpl<Scalar, Size, VecteurBackend::Generic, true, Derived> {
private:
    using Base = VecteurImpl<Scalar, Size, VecteurBackend::Generic, true, Derived>;

public:
    using Ref = std::add_lvalue_reference_t<Scalar>;
    using ConstRef = std::add_lvalue_reference_t<Scalar const>;
    using ConstexprImpl = Base;

public:
    VecteurImpl() = default;
    using Base::Base;
    using Base::operator=;

public:
    // Use the base class's implementation.
    using Base::add_;
    using Base::rsqrt_;
    using Base::sqrt_;

private:
    KIRA_FORCEINLINE auto strip_mining(auto const &loopBody) const {
        std::size_t i = 0;

        if constexpr (Derived::is_dynamic() or Derived::Size >= 4) {
            auto const tag = hn::ScalableTag<Scalar>();
            auto const lanes = hn::Lanes(tag);
            for (; i + lanes < this->size(); i += lanes)
                loopBody(tag, i);
        }

        for (; i < this->size(); ++i)
            loopBody(hn::CappedTag<Scalar, 1>(), i);
    }

public:
    /// Additionally implement the operation for the generic backend.
    ///
    /// Q: as for add, does this implementation gives better performance than the generic one?
    /// A: not necessarily. The generic implementation is already well-optimized through modern
    /// compilers.
    auto add_(Derived const &rhs) const {
        CheckDynamicOperable(this->derived(), rhs);

        auto result = Derived();
        if constexpr (Derived::is_dynamic())
            result.realloc(this->size());
        this->strip_mining([&](auto tag, std::size_t i) {
            auto const v1 = hn::LoadU(tag, this->data() + i);
            auto const v2 = hn::LoadU(tag, rhs.data() + i);
            hn::StoreU(hn::Add(v1, v2), tag, result.data() + i);
        });
        return result;
    }

    /// Vectorized square root.
    auto sqrt_() const {
        auto result = Derived();
        if constexpr (Derived::is_dynamic())
            result.realloc(this->size());
        this->strip_mining([&](auto tag, std::size_t i) {
            auto const v = hn::LoadU(tag, this->data() + i);
            hn::StoreU(hn::Sqrt(v), tag, result.data() + i);
        });
        return result;
    }

    /// Vectorized reciprocal square root.
    auto rsqrt_() const {
        auto result = Derived();
        if constexpr (Derived::is_dynamic())
            result.realloc(this->size());
        this->strip_mining([&](auto tag, std::size_t i) {
            auto const v = hn::LoadU(tag, this->data() + i);
            hn::StoreU(hn::ApproximateReciprocalSqrt(v), tag, result.data() + i);
        });
        return result;
    }
};
#endif
} // namespace kira::vecteur
