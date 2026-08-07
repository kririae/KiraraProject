#pragma once

#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace flux {
/// \brief A pool of values computed by key.
///
/// Requests for the same key use one computation. Requests for different keys
/// can compute in parallel. A failed key can be requested again.
template <typename Key, typename Value, typename Hash = std::hash<Key>> class ConcurrentPool {
    static_assert(std::is_nothrow_copy_constructible_v<Value>);

public:
    /// \brief The value for \p key.
    ///
    /// \pre \p key stays unchanged until this function returns.
    /// \pre Key copy, hash, and equality operations do not throw exceptions.
    /// \pre \p compute does not acquire values from this pool.
    template <typename Compute> Value acquire(Key const &key, Compute &&compute) {
        std::shared_ptr<Entry> entry;
        auto owner = false;
        {
            std::scoped_lock lock(mutex_);
            auto [iterator, inserted] = entries_.try_emplace(key);
            if (inserted) {
                try {
                    iterator->second = std::make_shared<Entry>();
                } catch (...) {
                    entries_.erase(iterator);
                    throw;
                }
                owner = true;
            }
            entry = iterator->second;
        }

        if (!owner)
            return observe(entry);

        try {
            entry->value.emplace(std::invoke(std::forward<Compute>(compute)));
            entry->state.store(State::Ready, std::memory_order_release);
            entry->state.notify_all();
            return *entry->value;
        } catch (...) {
            entry->error = std::current_exception();
            {
                std::scoped_lock lock(mutex_);
                auto const iterator = entries_.find(key);
                if (iterator != entries_.end() && iterator->second == entry)
                    entries_.erase(iterator);
            }
            entry->state.store(State::Failed, std::memory_order_release);
            entry->state.notify_all();
            std::rethrow_exception(entry->error);
        }
    }

private:
    enum class State : std::uint8_t {
        Pending,
        Ready,
        Failed,
    };

    struct Entry {
        std::atomic<State> state{State::Pending};
        std::optional<Value> value;
        std::exception_ptr error;
    };

    [[nodiscard]] static Value observe(std::shared_ptr<Entry> const &entry) {
        auto state = entry->state.load(std::memory_order_acquire);
        while (state == State::Pending) {
            entry->state.wait(State::Pending, std::memory_order_acquire);
            state = entry->state.load(std::memory_order_acquire);
        }
        if (state == State::Ready)
            return *entry->value;
        std::rethrow_exception(entry->error);
    }

    std::mutex mutex_;
    std::unordered_map<Key, std::shared_ptr<Entry>, Hash> entries_;
};
} // namespace flux
