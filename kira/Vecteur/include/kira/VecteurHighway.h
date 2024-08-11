#pragma once

#include <hwy/highway.h>

#include "kira/VecteurBase.h"
#include "kira/VecteurTraits.h"

namespace kira {
namespace hn = hwy::HWY_NAMESPACE;

#if 0
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
    using Base::sqrt_;

    /// Additionally implement the operation for the generic backend.
    ///
    /// Q: as for add, does this implementation gives better performance than the generic one?
    /// A: not necessarily. The generic implementation is already well-optimized through modern
    /// compilers.
    auto add_(Derived const &rhs) const
        requires(false)
    {
        CheckDynamicOperable(this->derived(), rhs);
        auto result = Derived();
        if constexpr (Derived::is_dynamic())
            result.realloc(this->size());

        std::size_t i = 0;

        // NOTE(krr): indeed, Size==3 is successfully vectorized.
        if constexpr (Derived::is_dynamic() or Derived::Size >= 4) {
            hn::ScalableTag<Scalar> tag;
            auto const lanes = hn::Lanes(tag);
            for (; i + lanes < this->size(); i += lanes) {
                auto const l = hn::LoadU(tag, this->data() + i);
                auto const r = hn::LoadU(tag, rhs.data() + i);
                hn::StoreU(hn::Add(l, r), tag, result.data() + i);
            }
        }

        // Strip-mining the remaining elements.
        for (; i < this->size(); ++i)
            result.entry(i) = this->entry(i) + rhs.entry(i);
        return result;
    }

    auto sqrt_() const {
        auto result = Derived();
        if constexpr (Derived::is_dynamic())
            result.realloc(this->size());

        std::size_t i = 0;
        if constexpr (Derived::is_dynamic() or Derived::Size >= 4) {
            hn::ScalableTag<Scalar> tag;
            auto const lanes = hn::Lanes(tag);
            for (; i + lanes < this->size(); i += lanes) {
                auto const v = hn::LoadU(tag, this->data() + i);
                hn::StoreU(hn::Sqrt(v), tag, result.data() + i);
            }
        }

        for (; i < this->size(); ++i)
            result.entry(i) = std::sqrt(this->entry(i));
        return result;
    }
};
#endif
} // namespace kira
