#pragma once

#include <atomic>

#include "Core/KIRA.h"

namespace krd {

/// \brief A CRTP mixin class that adds reference counting to a type.
///
/// By inheriting from this class, the object's lifetime is managed by `incRef()` and `decRef()`.
/// This class is not intended for heap allocation.
template <class Derived> class RefCountedMixin {
    RefCountedMixin() = default;
    RefCountedMixin(RefCountedMixin const &other) {}

public:
    virtual ~RefCountedMixin() = default;
    RefCountedMixin &operator=(RefCountedMixin const &other) = delete;

    /// Increases the reference count.
    void incRef() const { ++refCount; }

    /// Decreases the reference count. When the \c refCount is decreased to zero, the object will be
    /// released through \c delete.
    ///
    /// \return \c true if the object is released. Can be discarded.
    bool decRef() const {
        auto const newRefCount = refCount.fetch_sub(1);
        KRD_ASSERT(newRefCount > 0, "Object reference count must be > 0");
        if (newRefCount == 1) {
            delete static_cast<Derived const *>(this);
            return true;
        }

        return false;
    }

    /// Gets the current reference count.
    [[nodiscard]] auto getRefCount() const { return refCount.load(); }

private:
    mutable std::atomic_uint32_t refCount{0};
    friend Derived;
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
    ~Object() override = default;
};

/// A reference to an object that is not reference-counted.
template <typename T> class UniqueRef {
    using element_type = T;

public:
    /// Default constructor
    UniqueRef() = default;

    /// Construct a UniqueRef from nullptr
    UniqueRef(std::nullptr_t) noexcept {}

    /// Destructor
    ~UniqueRef() {
        if (ptr)
            delete ptr;
    }

    // Delete copy constructor and assignment operator
    UniqueRef(UniqueRef const &r) = delete;
    UniqueRef &operator=(UniqueRef const &r) = delete;

    /// Construct from a raw pointer
    UniqueRef(T *p) noexcept : ptr(p) {}

    /// Move constructor
    UniqueRef(UniqueRef &&r) noexcept : ptr(std::exchange(r.ptr, nullptr)) {}

    /// Move assignment operator
    UniqueRef &operator=(UniqueRef &&r) noexcept {
        swap(*this, r);
        return *this;
    }

    friend void swap(UniqueRef &lhs, UniqueRef &rhs) noexcept {
        using std::swap;
        swap(lhs.ptr, rhs.ptr);
    }

public:
    // NOTE(krr): No *observable state change*
    [[nodiscard]] bool operator==(UniqueRef const &r) const noexcept { return ptr == r.ptr; }
    [[nodiscard]] bool operator!=(UniqueRef const &r) const noexcept { return ptr != r.ptr; }
    [[nodiscard]] bool operator==(T const *otherPtr) const noexcept { return ptr == otherPtr; }
    [[nodiscard]] bool operator!=(T const *otherPtr) const noexcept { return ptr != otherPtr; }
    [[nodiscard]] T *operator->() const noexcept { return ptr; }
    [[nodiscard]] T &operator*() const { return *ptr; }
    [[nodiscard]] explicit operator T *() const noexcept { return ptr; }
    [[nodiscard]] T *get() const noexcept { return ptr; }
    [[nodiscard]] explicit operator bool() const noexcept { return ptr != nullptr; }

public:
    /// Reset the \c UniqueRef to nullptr
    void reset() noexcept {
        UniqueRef other{};
        swap(*this, other);
    }

    /// Release ownership of the managed object
    [[nodiscard]] T *release() noexcept { return std::exchange(ptr, nullptr); }

private:
    T *ptr{nullptr};
};

/// Reference to an object that inherits from \c Object.
template <typename T> class Ref {
    using element_type = T;

public:
    template <typename T2> friend class Ref;
    template <typename T2> friend class UniqueRef;

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

    Ref(Ref &&r) noexcept : ptr(std::exchange(r.ptr, nullptr)) {}

    template <typename T2>
        requires(std::is_convertible_v<T2 *, T *>)
    Ref(T2 *ptr) // NOLINT: allow implicit conversion
        : ptr(ptr) {
        if (ptr)
            ptr->incRef();
    }

#if 0
    template <typename T2>
        requires(std::is_convertible_v<T2 *, T *>)
    Ref(T2 const *ptr) // NOLINT: allow implicit conversion
        : ptr(ptr) {
        if (ptr)
            ptr->incRef();
    }
#endif

    template <typename T2>
        requires(std::is_convertible_v<T2 *, T *>)
    Ref(Ref<T2> const &r) // NOLINT: allow implicit conversion
        : ptr(static_cast<T *>(r.get())) {
        if (ptr)
            ptr->incRef();
    }

    template <typename T2>
        requires(std::is_convertible_v<T2 *, T *>)
    Ref(Ref<T2> &&r) // NOLINT: allow implicit conversion
        : ptr(static_cast<T *>(std::exchange(r.ptr, nullptr))) {}

    // see https://en.cppreference.com/w/cpp/memory/shared_ptr/shared_ptr
    template <typename T2>
        requires(std::is_convertible_v<T2 *, T *>)
    Ref(UniqueRef<T2> &&r) // NOLINT: allow implicit conversion
        : ptr(static_cast<T *>(r.release())) {
        if (ptr)
            ptr->incRef();
    }

    friend void swap(Ref &lhs, Ref &rhs) noexcept {
        using std::swap;
        swap(lhs.ptr, rhs.ptr);
    }

    Ref &operator=(Ref r) noexcept {
        swap(*this, r);
        return *this;
    }

public:
    // NOTE(krr): No *observable state change*
    [[nodiscard]] bool operator==(Ref const &r) const noexcept { return ptr == r.ptr; }
    [[nodiscard]] bool operator!=(Ref const &r) const noexcept { return ptr != r.ptr; }
    [[nodiscard]] bool operator==(T const *otherPtr) const noexcept { return ptr == otherPtr; }
    [[nodiscard]] bool operator!=(T const *otherPtr) const noexcept { return ptr != otherPtr; }
    [[nodiscard]] T *operator->() const noexcept { return ptr; }
    [[nodiscard]] T &operator*() const { return *ptr; }
    [[nodiscard]] explicit operator T *() const noexcept { return ptr; }
    [[nodiscard]] T *get() const noexcept { return ptr; }
    [[nodiscard]] explicit operator bool() const noexcept { return ptr != nullptr; }

public:
    /// Gets the current reference count.
    [[nodiscard]] auto getRefCount() const { return ptr != nullptr ? ptr->getRefCount() : 0; }

    /// Checks if the reference is exclusive.
    [[nodiscard]] bool isExclusive() const noexcept { return ptr != nullptr && getRefCount() == 1; }

    /// Resets the reference to \c nullptr.
    void reset() noexcept {
        Ref other{};
        swap(*this, other);
    }

    /// Cast the \c Ref<T> to \c Ref<T2> using \c dynamic_cast.
    ///
    /// \remark New reference count is added.
    /// \remark If the cast fails, the returned \c Ref<T2> will be \c nullptr.
    template <typename T2> [[nodiscard]] auto dyn_cast() const {
        return Ref<T2>{dynamic_cast<T2 *>(ptr)};
    }

#if 0
    /// Decay the \c Ref<T> to \c UniqueRef<T> with runtime check.
    ///
    /// \return If the reference count is 1, the ownership is transferred to the \c UniqueRef<T>.
    /// Else, an empty \c UniqueRef will be returned.
    UniqueRef<T> tryDecay() && noexcept {
        if (getRefCount() == 1)
            return UniqueRef<T>(std::exchange(ptr, nullptr));
        return {nullptr};
    }

    /// Decay the \c Ref<T> to \c UniqueRef<T> with runtime check.
    ///
    /// \throw kira::Anyhow if the reference count is not 1.
    UniqueRef<T> decay() && {
        if (getRefCount() == 1)
            return UniqueRef<T>(std::exchange(ptr, nullptr));
        throw kira::Anyhow("Ref: Failed to decay the reference");
    }
#endif

private:
    T *ptr{nullptr};
};

/// Make a \c UniqueRef<T> with the given arguments.
template <typename T, typename... Args> [[nodiscard]] UniqueRef<T> make_unique_ref(Args &&...args) {
    return UniqueRef<T>(new T(std::forward<Args>(args)...));
}

/// Make a \c Ref<T> with the given arguments.
template <typename T, typename... Args> [[nodiscard]] Ref<T> make_ref(Args &&...args) {
    return Ref<T>(new T(std::forward<Args>(args)...));
}
} // namespace krd
