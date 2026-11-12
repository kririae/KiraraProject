#pragma once

#include <concepts>
#include <cstddef>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

#include "flux/Core/Object.h"
#include "kira/Assertions.h"

namespace flux {
/// \brief Shares an immutable value that has no identity.
///
/// \c Ref stays for objects with an identity.
template <typename T> using Shared = std::shared_ptr<T const>;

namespace detail {
/// \brief Movable container of contiguous \p T, passed as an rvalue.
template <typename C, typename T>
concept ContiguousContainer =
    std::same_as<C, std::remove_cvref_t<C>> && std::movable<C> && requires(C &container) {
        { container.data() } -> std::same_as<T *>;
        { container.size() } -> std::convertible_to<std::size_t>;
        { container.capacity() } -> std::convertible_to<std::size_t>;
    };
} // namespace detail

/// \brief Owns a contiguous array in host memory.
///
/// \tparam T Trivially copyable element type.
template <typename T> class HostBuffer final : private Noncopyable {
    static_assert(!std::is_const_v<T>, "HostBuffer cannot store const elements");
    static_assert(
        std::is_trivially_copyable_v<T>, "HostBuffer requires trivially copyable elements"
    );

public:
    using value_type = T;

    /// \brief Constructs an empty buffer.
    HostBuffer() noexcept = default;

    /// \brief Takes \p container, which owns its elements alone.
    ///
    /// An empty \p container gives an empty buffer.
    /// \param container Rvalue container with \c data, \c size, and \c capacity.
    template <typename Container>
        requires(
            !std::same_as<std::remove_cvref_t<Container>, HostBuffer> &&
            detail::ContiguousContainer<Container, T>
        )
    explicit HostBuffer(Container &&container) {
        if (container.size() == 0)
            return;

        // Move the container to the heap, so the buffer's own moves never move it.
        auto *held = new Container(std::move(container));
        owner_ = Owner(held, [](void *owned) noexcept { delete static_cast<Container *>(owned); });

        // Read the pointer after the move: a container with inline storage changes it.
        data_ = held->data();
        size_ = held->size();
        capacity_ = held->capacity();
    }

    /// \brief Takes ownership of \p other and leaves it empty.
    HostBuffer(HostBuffer &&other) noexcept { swap(*this, other); }

    /// \brief Replaces this allocation with the one owned by \p other.
    HostBuffer &operator=(HostBuffer &&other) noexcept {
        HostBuffer replacement(std::move(other));
        swap(*this, replacement);
        return *this;
    }

    /// \brief Replaces the allocation without preserving its contents.
    ///
    /// A \p count of zero releases the allocation, whatever \p capacity is. The
    /// elements are uninitialized.
    /// \param count Number of elements in the replacement.
    /// \param capacity Number of elements readable from data(), at least \p count.
    void resize(std::size_t count, std::size_t capacity) {
        KIRA_FORCE_ASSERT(capacity >= count, "HostBuffer capacity is below its size");
        if (count == 0) {
            clear();
            return;
        }

        // Allocate before releasing, so a failed allocation keeps the old array.
        auto *array = new T[capacity];
        owner_ = Owner(array, [](void *owned) noexcept { delete[] static_cast<T *>(owned); });

        // Point at the new array.
        data_ = array;
        size_ = count;
        capacity_ = capacity;
    }

    /// \brief Replaces the allocation with \p count elements and no spare capacity.
    void resize(std::size_t count) { resize(count, count); }

    /// \brief Releases the allocation.
    void clear() noexcept {
        owner_.reset();
        data_ = nullptr;
        size_ = 0;
        capacity_ = 0;
    }

    /// \brief Returns the host pointer, or null when empty.
    [[nodiscard]] T *data() noexcept { return data_; }

    /// \brief Returns the host pointer, or null when empty.
    [[nodiscard]] T const *data() const noexcept { return data_; }

    /// \brief Returns the number of stored elements.
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    /// \brief Returns the number of elements readable from data().
    ///
    /// It is at least size(), and zero when empty.
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    /// \brief Returns whether the buffer is empty.
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    /// \brief Returns a mutable view of the stored elements.
    [[nodiscard]] std::span<T> span() noexcept { return {data_, size_}; }

    /// \brief Returns a read-only view of the stored elements.
    [[nodiscard]] std::span<T const> span() const noexcept { return {data_, size_}; }

    friend void swap(HostBuffer &lhs, HostBuffer &rhs) noexcept {
        using std::swap;
        swap(lhs.owner_, rhs.owner_);
        swap(lhs.data_, rhs.data_);
        swap(lhs.size_, rhs.size_);
        swap(lhs.capacity_, rhs.capacity_);
    }

private:
    using Owner = std::unique_ptr<void, void (*)(void *) noexcept>;

    /// Type-erased owner of the storage behind data_: an array or a container.
    Owner owner_{nullptr, nullptr};
    /// First element, or null exactly when size_ is zero. capacity_ is then zero too.
    T *data_{};
    std::size_t size_{};
    std::size_t capacity_{};
};
} // namespace flux
