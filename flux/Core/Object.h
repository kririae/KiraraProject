#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "kira/Assertions.h"
#include "kira/Properties.h"

namespace flux {
class Context;
class TXContext;

/// \brief Utility base that disables copy construction and assignment.
class Noncopyable {
protected:
    constexpr Noncopyable() noexcept = default;
    ~Noncopyable() = default;
    Noncopyable(Noncopyable &&) noexcept = default;
    Noncopyable &operator=(Noncopyable &&) noexcept = default;

public:
    Noncopyable(Noncopyable const &) = delete;
    Noncopyable &operator=(Noncopyable const &) = delete;
};

/// \brief Adds intrusive reference counting to an object.
template <typename Derived> class RefCountedBase : private Noncopyable {
protected:
    RefCountedBase() = default;
    ~RefCountedBase() = default;

public:
    /// \brief Adds one owning reference.
    void incrementRef() const noexcept { refCount_.fetch_add(1, std::memory_order_relaxed); }

    /// \brief Releases one owning reference and deletes the object at zero.
    void decrementRef() const noexcept {
        auto const previous = refCount_.fetch_sub(1, std::memory_order_acq_rel);
        KIRA_FORCE_ASSERT(previous > 0, "Object reference count must be positive");
        if (previous == 1)
            delete static_cast<Derived const *>(this);
    }

    /// \brief Returns the number of owning references.
    [[nodiscard]] std::uint32_t getRefCount() const noexcept {
        return refCount_.load(std::memory_order_relaxed);
    }

private:
    mutable std::atomic_uint32_t refCount_{0};
};

/// \brief An owning reference to an intrusive reference-counted object.
template <typename T> class Ref {
public:
    using element_type = T;

    template <typename U> friend class Ref;

    /// \brief Constructs an empty reference.
    Ref() noexcept = default;

    /// \brief Constructs an empty reference.
    Ref(std::nullptr_t) noexcept {}

    /// \brief Takes a reference to \p pointer.
    ///
    /// \pre \p pointer is null or points to a heap-allocated object managed by
    /// \c Ref.
    template <typename U>
        requires(std::is_convertible_v<U *, T *>)
    explicit Ref(U *pointer) noexcept : pointer_(pointer) {
        if (pointer_)
            pointer_->incrementRef();
    }

    /// \brief Copies an owning reference.
    Ref(Ref const &other) noexcept : Ref(other.pointer_) {}

    /// \brief Converts and copies a compatible owning reference.
    template <typename U>
        requires(std::is_convertible_v<U *, T *>)
    Ref(Ref<U> const &other) noexcept : Ref(other.pointer_) {}

    /// \brief Moves an owning reference.
    Ref(Ref &&other) noexcept : pointer_(std::exchange(other.pointer_, nullptr)) {}

    /// \brief Converts and moves a compatible owning reference.
    template <typename U>
        requires(std::is_convertible_v<U *, T *>)
    Ref(Ref<U> &&other) noexcept : pointer_(std::exchange(other.pointer_, nullptr)) {}

    ~Ref() {
        if (pointer_)
            pointer_->decrementRef();
    }

    /// \brief Replaces this reference with \p other.
    Ref &operator=(Ref other) noexcept {
        swap(*this, other);
        return *this;
    }

    /// \brief Exchanges two references.
    friend void swap(Ref &lhs, Ref &rhs) noexcept {
        using std::swap;
        swap(lhs.pointer_, rhs.pointer_);
    }

    /// \brief Returns the stored pointer.
    [[nodiscard]] T *get() const noexcept { return pointer_; }

    /// \brief Returns the referenced object.
    [[nodiscard]] T &operator*() const noexcept { return *pointer_; }

    /// \brief Returns the stored pointer for member access.
    [[nodiscard]] T *operator->() const noexcept { return pointer_; }

    /// \brief Returns whether this reference owns an object.
    [[nodiscard]] explicit operator bool() const noexcept { return pointer_ != nullptr; }

    /// \brief Returns the object's current reference count.
    [[nodiscard]] std::uint32_t getRefCount() const noexcept {
        return pointer_ ? pointer_->getRefCount() : 0;
    }

    /// \brief Releases this reference.
    void reset() noexcept {
        Ref other;
        swap(*this, other);
    }

    /// \brief Dynamically casts this reference to \c Ref<U>.
    template <typename U> [[nodiscard]] Ref<U> dynamicCast() const noexcept {
        return Ref<U>{dynamic_cast<U *>(pointer_)};
    }

    [[nodiscard]] friend bool operator==(Ref const &lhs, Ref const &rhs) noexcept {
        return lhs.pointer_ == rhs.pointer_;
    }

    [[nodiscard]] friend bool operator==(Ref const &reference, std::nullptr_t) noexcept {
        return reference.pointer_ == nullptr;
    }

private:
    T *pointer_{nullptr};
};

/// \brief Base class for objects managed by \c Ref.
class Object : public RefCountedBase<Object> {
protected:
    /// \brief Constructs an object with no owning references.
    Object() = default;

public:
    /// \brief Destroys the object after its final reference is released.
    virtual ~Object() = default;
};

/// \brief An object owned by a \c Context.
class ContextObject : public Object {
    friend class Context;
    friend class TXContext;

protected:
    /// \brief Assigns an owner and stable ID from \p tx.
    explicit ContextObject(TXContext &tx);

    /// \brief Registers a fully constructed object with \p tx.
    virtual void registerTo(TXContext &tx);

public:
    /// \brief Returns the owning context, or null after that context is destroyed.
    [[nodiscard]] Context *getContext() noexcept { return context_; }

    /// \brief Returns the owning context, or null after that context is destroyed.
    [[nodiscard]] Context const *getContext() const noexcept { return context_; }

    /// \brief Returns the stable identifier assigned by the owning context.
    [[nodiscard]] std::size_t getContextId() const noexcept { return contextId_; }

private:
    Context *context_;
    std::size_t contextId_;
};

/// \brief A context object constructed from \c kira::Properties.
class ConfigurableObject : public ContextObject {
protected:
    /// \brief Stores \p properties with the object created in \p tx.
    ConfigurableObject(TXContext &tx, kira::Properties properties);

public:
    /// \brief Returns the properties used to construct this object.
    [[nodiscard]] kira::Properties const &getProperties() const noexcept { return properties_; }

private:
    kira::Properties properties_;
};

/// \brief Matches objects owned by a \c Context.
template <typename T>
concept IsContextObject = std::derived_from<T, ContextObject>;

/// \brief Matches context objects constructed from \c kira::Properties.
template <typename T>
concept IsConfigurableObject = std::derived_from<T, ConfigurableObject>;
} // namespace flux
