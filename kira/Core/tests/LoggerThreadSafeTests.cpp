#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "kira/Logger.h"

TEST(LoggerThreadSafeTests, GetLoggerThreadSafety) {
    constexpr int numThreads = 32;
    constexpr int iterPerThread = 128;

    std::atomic<int> threadsReady(0);
    std::atomic<bool> startFlag(false);
    std::vector<std::thread> threads;
    std::atomic<int> successCount(0);

    auto threadFunction = [&]() {
        threadsReady++;
        while (!startFlag.load())
            std::this_thread::yield();

        for (int i = 0; i < iterPerThread; ++i) {
            auto *logger = kira::GetLogger<"testLogger">();
            if (logger != nullptr)
                successCount++;
        }
    };

    threads.reserve(numThreads);
    for (int i = 0; i < numThreads; ++i)
        threads.emplace_back(threadFunction);

    while (threadsReady.load() < numThreads)
        std::this_thread::yield();

    startFlag.store(true);

    for (auto &thread : threads)
        thread.join();

    EXPECT_EQ(successCount.load(), numThreads * iterPerThread);

    auto *logger1 = kira::GetLogger<"testLogger">();
    auto *logger2 = kira::GetLogger<"testLogger">();
    EXPECT_EQ(logger1, logger2);
}
