#include <gtest/gtest.h>

#include <atomic>
#include <regex>
#include <thread>
#include <vector>

#include "kira/Logger.h"

using namespace kira;

class LoggerThreadSafeTests : public ::testing::Test {
protected:
    void SetUp() override {
        spdlog::shutdown();
#ifdef _WIN32
        _putenv_s("KRR_LOG_LEVEL", "info");
#else
        setenv("KRR_LOG_LEVEL", "info", 1);
#endif
    }

    void TearDown() override {
#ifdef _WIN32
        _putenv_s("KRR_LOG_LEVEL", "");
#else
        unsetenv("KRR_LOG_LEVEL");
#endif
    }
};

TEST_F(LoggerThreadSafeTests, GetLoggerThreadSafety) {
    constexpr int numThreads = 32;
    constexpr int iterPerThread = 1024;

    std::atomic<int> threadsReady(0);
    std::atomic<bool> startFlag(false);
    std::vector<std::thread> threads;
    std::atomic<int> successCount(0);

    auto threadFunction = [&]() {
        threadsReady++;
        while (!startFlag.load())
            std::this_thread::yield();

        for (int i = 0; i < iterPerThread; ++i) {
            auto *logger = GetLogger("testLogger");
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

    auto *logger1 = GetLogger("testLogger");
    auto *logger2 = GetLogger("testLogger");
    EXPECT_EQ(logger1, logger2);
}

#ifndef _WIN32
TEST_F(LoggerThreadSafeTests, LoggingFunctionsThreadSafety) {
    // Thanks again to Claude for this test.
    constexpr int numThreads = 32;
    constexpr int iterPerThread = 32;

    std::atomic<int> threadsReady(0);
    std::atomic<bool> startFlag(false);
    std::vector<std::thread> threads;

    auto threadFunction = [&](int threadId) {
        threadsReady++;
        while (!startFlag.load())
            std::this_thread::yield();

        for (int i = 0; i < iterPerThread; ++i)
            LogInfo("Thread {} iteration {}", threadId, i);
    };

    ::testing::internal::CaptureStdout();
    threads.reserve(numThreads);
    for (int i = 0; i < numThreads; ++i)
        threads.emplace_back(threadFunction, i);

    while (threadsReady.load() < numThreads)
        std::this_thread::yield();

    startFlag.store(true);

    for (auto &thread : threads)
        thread.join();
    auto const output = ::testing::internal::GetCapturedStdout();

    std::vector<std::vector<int>> threadIterations(numThreads);
    std::regex logPattern(R"(Thread (\d+) iteration (\d+))");
    std::smatch match;
    std::string::const_iterator searchStart(output.cbegin());

    while (std::regex_search(searchStart, output.cend(), match, logPattern)) {
        auto const threadId = std::stoi(match[1]);
        auto const iteration = std::stoi(match[2]);
        threadIterations[threadId].push_back(iteration);
        searchStart = match.suffix().first;
    }

    for (int i = 0; i < numThreads; ++i) {
        EXPECT_EQ(threadIterations[i].size(), iterPerThread)
            << "Thread " << i << " did not log the expected number of iterations";
        EXPECT_TRUE(std::is_sorted(threadIterations[i].begin(), threadIterations[i].end()))
            << "Iterations for thread " << i << " are not in order";
    }
}
#endif
