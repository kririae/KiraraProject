#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <latch>
#include <stdexcept>
#include <string>
#include <vector>

#include "flux/Core/ConcurrentPool.h"

TEST(ConcurrentPoolTests, CoalescesConcurrentRequestsForOneKey) {
    flux::ConcurrentPool<int, int> pool;
    std::atomic_uint32_t computeCount{};
    std::latch computeStarted{1};
    std::latch releaseCompute{1};

    auto owner = std::async(std::launch::async, [&] {
        return pool.acquire(7, [&] {
            computeCount.fetch_add(1, std::memory_order_relaxed);
            computeStarted.count_down();
            releaseCompute.wait();
            return 42;
        });
    });
    computeStarted.wait();

    std::vector<std::future<int>> waiters;
    for (auto index = 0; index < 8; ++index) {
        waiters.push_back(std::async(std::launch::async, [&] {
            return pool.acquire(7, [&] {
                computeCount.fetch_add(1, std::memory_order_relaxed);
                return 0;
            });
        }));
    }

    releaseCompute.count_down();
    EXPECT_EQ(owner.get(), 42);
    for (auto &waiter : waiters)
        EXPECT_EQ(waiter.get(), 42);
    EXPECT_EQ(
        pool.acquire(
            7,
            [&] {
        computeCount.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
        ),
        42
    );
    EXPECT_EQ(computeCount.load(std::memory_order_relaxed), 1U);
}

TEST(ConcurrentPoolTests, ComputesDifferentKeysInParallel) {
    using namespace std::chrono_literals;

    flux::ConcurrentPool<int, int> pool;
    std::promise<void> releaseComputes;
    auto const release = releaseComputes.get_future().share();
    std::promise<void> firstStarted;
    std::promise<void> secondStarted;
    auto secondStartedFuture = secondStarted.get_future();

    auto first = std::async(std::launch::async, [&] {
        return pool.acquire(1, [&] {
            firstStarted.set_value();
            release.wait();
            return 10;
        });
    });
    firstStarted.get_future().wait();
    auto second = std::async(std::launch::async, [&] {
        return pool.acquire(2, [&] {
            secondStarted.set_value();
            release.wait();
            return 20;
        });
    });

    auto const secondStatus = secondStartedFuture.wait_for(1s);
    releaseComputes.set_value();
    EXPECT_EQ(secondStatus, std::future_status::ready);
    EXPECT_EQ(first.get(), 10);
    EXPECT_EQ(second.get(), 20);
}

TEST(ConcurrentPoolTests, PublishesComputationErrorsToWaiters) {
    using namespace std::chrono_literals;

    flux::ConcurrentPool<int, int> pool;
    std::atomic_uint32_t computeCount{};
    std::latch computeStarted{1};
    std::latch releaseCompute{1};

    auto owner = std::async(std::launch::async, [&] {
        return pool.acquire(7, [&]() -> int {
            computeCount.fetch_add(1, std::memory_order_relaxed);
            computeStarted.count_down();
            releaseCompute.wait();
            throw std::runtime_error("first failure");
        });
    });
    computeStarted.wait();

    auto waiter = std::async(std::launch::async, [&] {
        return pool.acquire(7, [&] {
            computeCount.fetch_add(1, std::memory_order_relaxed);
            return 0;
        });
    });
    auto const waiterStatus = waiter.wait_for(100ms);
    releaseCompute.count_down();

    EXPECT_EQ(waiterStatus, std::future_status::timeout);
    try {
        (void)owner.get();
        FAIL() << "owner returned a value";
    } catch (std::runtime_error const &error) {
        EXPECT_EQ(std::string{error.what()}, "first failure");
    }
    try {
        (void)waiter.get();
        FAIL() << "waiter returned a value";
    } catch (std::runtime_error const &error) {
        EXPECT_EQ(std::string{error.what()}, "first failure");
    }
    EXPECT_EQ(computeCount.load(std::memory_order_relaxed), 1U);
}

TEST(ConcurrentPoolTests, RetriesAfterAComputationError) {
    flux::ConcurrentPool<int, int> pool;
    auto computeCount = 0;

    EXPECT_THROW(
        (void)pool.acquire(
            7,
            [&]() -> int {
        ++computeCount;
        throw std::runtime_error("failed");
    }
        ),
        std::runtime_error
    );
    EXPECT_EQ(
        pool.acquire(
            7,
            [&] {
        ++computeCount;
        return 42;
    }
        ),
        42
    );
    EXPECT_EQ(computeCount, 2);
}
