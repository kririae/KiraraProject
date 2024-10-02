#pragma once

#include <atomic>

#include "Core/KIRA.h"

namespace krd {

/// \brief A CRTP mixin class that adds reference counting to a type.
///
/// By inheriting from this class, the object's lifetime is managed by `incRef()` and `decRef()`.
/// This class is not intended for heap allocation.
template <class Derived> class RefCountedMixin {
public:
    ~RefCountedMixin() = default;

    /// Increases the reference count.
    void incRef() const { ++refCount; }

    /// Decreases the reference count. When the \c refCount is decreased to zero, the object will be
    /// released through \c delete.
    void decRef() const {
        auto const newRefCount = refCount.fetch_sub(1);
        KRD_ASSERT(newRefCount > 0, "Object reference count must be > 0");
        if (newRefCount == 1)
            delete static_cast<Derived const *>(this);
    }

    /// Gets the current reference count.
    [[nodiscard]] auto getRefCount() const { return refCount.load(); }

private:
    mutable std::atomic_uint32_t refCount{0};
};

/// The reference-counted base class.
///
/// Classes that inherit from this class must satisfy the following requirements:
/// \li The class must have a public destructor.
/// \li The class must have only either a private or protected constructor.
/// \li The class should have a static \c create() method that returns a \c Ref<T>.
class Object : public RefCountedMixin<Object> {
protected:
    Object() = default;

public:
    virtual ~Object() = default;
};

/// Reference to an object that inherits from \c Object.
template <typename T> class Ref {
    using element_type = T;

public:
    /// Default constructor.
    Ref() = default;

    /// Construct a \c Ref from \c nullptr.
    Ref(std::nullptr_t) noexcept {}

    ~Ref() {
        if (ptr)
            ptr->decRef();
    }

    Ref(Ref const &r) noexcept : ptr(static_cast<T *>(r.get())) {
        if (ptr)
            ptr->incRef();
    }

    Ref(Ref &&r) noexcept : ptr(r.ptr) { r.ptr = nullptr; }

    template <typename T2>
        requires(std::is_convertible_v<T2 *, T *>)
    Ref(T2 *ptr) : ptr(ptr) { // NOLINT: allow implicit conversion
        if (ptr)
            ptr->incRef();
    }

    template <typename T2>
        requires(std::is_convertible_v<T2 *, T *>)
    Ref(Ref<T2> const &r) // NOLINT: allow implicit conversion
        : ptr(static_cast<T *>(r.get())) {
        if (ptr)
            ptr->incRef();
    }

public:
    void swap(Ref &lhs, Ref &rhs) noexcept {
        using std::swap;
        swap(lhs.ptr, rhs.ptr);
    }

    Ref &operator=(Ref r) noexcept {
        swap(*this, r);
        return *this;
    }

public:
    // NOTE(krr): No *observable state change*

    [[nodiscard]] bool operator==(Ref const &r) const { return ptr == r.ptr; }
    [[nodiscard]] bool operator!=(Ref const &r) const { return ptr != r.ptr; }
    [[nodiscard]] bool operator==(T const *otherPtr) const { return ptr == otherPtr; }
    [[nodiscard]] bool operator!=(T const *otherPtr) const { return ptr != otherPtr; }
    [[nodiscard]] T *operator->() const { return ptr; }
    [[nodiscard]] T &operator*() const { return *ptr; }
    [[nodiscard]] explicit operator T *() const { return ptr; }
    [[nodiscard]] T *get() const noexcept { return ptr; }
    [[nodiscard]] explicit operator bool() const { return ptr != nullptr; }

public:
    /// Gets the current reference count.
    [[nodiscard]] auto getRefCount() const { return ptr != nullptr ? ptr->getRefCount() : 0; }

    /// Resets the reference to \c nullptr.
    void reset() noexcept {
        Ref other{};
        swap(*this, other);
    }

    /// Cast the \c Ref<T> to \c Ref<T2> using \c dynamic_cast.
    ///
    /// \remark No new reference count is added.
    /// \remark If the cast fails, the returned \c Ref<T2> will be \c nullptr.
    template <typename T2>
    [[nodiscard]]
    auto dyn_cast() const {
        return Ref<T2>{dynamic_cast<T2 *>(ptr)};
    }

private:
    T *ptr{nullptr};
};
} // namespace krd
