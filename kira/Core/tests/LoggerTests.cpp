/*
 * This file is part of AEROCAE, a computational fluid dynamics system.
 *
 * Created Date: Thursday, August 8th 2024, 12:05:23 am
 * Author: Zike Xu
 * -----
 * Last Modified: Wed Aug 07 2024
 * Modified By: Zike Xu
 * -----
 * Copyright (c) 2024 AEROCAE
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <random>
#include <stdexcept>

#include "kira/Logger.h"

using namespace kira;

class LoggerTests : public ::testing::Test {
protected:
    std::filesystem::path tempLogPath;

    void SetUp() override {
        std::string randomName = GenerateRandomString(10) + ".log";
        tempLogPath = std::filesystem::temp_directory_path() / randomName;

        if (std::filesystem::exists(tempLogPath))
            std::filesystem::remove(tempLogPath);
        spdlog::shutdown();
        setenv("KRR_LOG_LEVEL", "info", 1);
    }

    void TearDown() override {
        spdlog::shutdown();
        if (std::filesystem::exists(tempLogPath)) {
            detail::SinkManager::GetInstance().DropAllSinks();
            std::filesystem::remove(tempLogPath);
        }
        unsetenv("KRR_LOG_LEVEL");
    }

    bool FileContainsLog(std::string const &logMessage) {
        std::ifstream file(tempLogPath);
        std::string line;
        while (std::getline(file, line))
            if (line.find(logMessage) != std::string::npos)
                return true;
        return false;
    }

    std::string GenerateRandomString(size_t length) {
        std::string const chars = "0123456789"
                                  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                  "abcdefghijklmnopqrstuvwxyz";

        std::random_device rd;
        std::mt19937 generator(rd());
        std::uniform_int_distribution<> distribution(0, chars.size() - 1);

        std::string result;
        for (size_t i = 0; i < length; ++i)
            result += chars[distribution(generator)];
        return result;
    }
};

TEST_F(LoggerTests, SetupLogger) {
    LoggerBuilder{"testSetup"}.to_console(true).init();
    auto logger = spdlog::get("testSetup");
    EXPECT_NE(logger, nullptr);
    EXPECT_EQ(logger->sinks().size(), 1);

    LoggerBuilder{"testSetupFile1"}.to_console(true).to_file(tempLogPath).init();
    logger = spdlog::get("testSetupFile1");
    EXPECT_NE(logger, nullptr);
    EXPECT_EQ(logger->sinks().size(), 2);

    LoggerBuilder{"testSetupFile2"}.to_console(true).to_file(tempLogPath).init();
    auto logger1 = spdlog::get("testSetupFile2");
    EXPECT_NE(logger1, nullptr);
    EXPECT_EQ(logger1->sinks().size(), 2);

    EXPECT_EQ(logger1->sinks().at(0), logger->sinks().at(0));
    EXPECT_EQ(logger1->sinks().at(1), logger->sinks().at(1));
}

TEST_F(LoggerTests, ShutdownFromSpdlog) {
    auto *logger = GetLogger("testLogger");
    EXPECT_NE(logger, nullptr);

    spdlog::shutdown();
    auto *logger2 = spdlog::get("testLogger").get();
    EXPECT_EQ(logger2, nullptr);

    auto *logger3 = GetLogger("testLogger");
    auto *logger4 = GetLogger("testLogger");
    EXPECT_EQ(logger3, logger4);
}

TEST_F(LoggerTests, DuplicateLogger) {
    LoggerBuilder{"testDuplicated"}.to_console(true).init();
    EXPECT_THROW(LoggerBuilder{"testDuplicated"}.to_console(true).init(), std::runtime_error);
}

// Disable these tests on Windows
#if !defined(_WIN32)
TEST_F(LoggerTests, LogToFileAndConsole) {
    LoggerBuilder{}.to_console(true).to_file(tempLogPath).init();
    EXPECT_NE(spdlog::get(defaultLoggerName.value).get(), nullptr);

    ::testing::internal::CaptureStdout();
    LogInfo("test message");
    LogFlush();
    auto const &output = ::testing::internal::GetCapturedStdout();
    EXPECT_TRUE(output.find("test message") != std::string::npos);
    EXPECT_TRUE(FileContainsLog("test message"));
}

TEST_F(LoggerTests, NoLogToConsole) {
    LoggerBuilder{}.to_console(false).init();
    ::testing::internal::CaptureStdout();
    LogInfo("test message");
    LogFlush();
    auto const &output = ::testing::internal::GetCapturedStdout();
    EXPECT_TRUE(output.find("test message") == std::string::npos);
}

TEST_F(LoggerTests, LogToConsole) {
    LoggerBuilder{}.to_console(true).init();
    ::testing::internal::CaptureStdout();
    LogInfo("test message");
    LogFlush();
    auto const &output = ::testing::internal::GetCapturedStdout();
    EXPECT_TRUE(output.find("test message") != std::string::npos);
}

TEST_F(LoggerTests, LogWithFormat) {
    ::testing::internal::CaptureStdout();
    LogInfo("test message {}", 42);
    LogFlush();
    auto const &output = ::testing::internal::GetCapturedStdout();
    EXPECT_TRUE(output.find("test message 42") != std::string::npos);
}

TEST_F(LoggerTests, LogWithFilterFromBuilder) {
    LoggerBuilder{}.to_console(true).filter_level(spdlog::level::warn).init();
    ::testing::internal::CaptureStdout();
    LogInfo("test info");
    LogFlush();
    LogWarn("test warn");
    LogFlush();
    auto const &output = ::testing::internal::GetCapturedStdout();
    EXPECT_TRUE(output.find("test info") == std::string::npos);
    EXPECT_TRUE(output.find("test warn") != std::string::npos);
}

TEST_F(LoggerTests, LogWithFilterFromEnvironement) {
    setenv("KRR_LOG_LEVEL", "warn", 1);
    LoggerBuilder{}.to_console(true).init();
    ::testing::internal::CaptureStdout();
    LogInfo("test info");
    LogFlush();
    LogWarn("test warn");
    LogFlush();
    auto const &output = ::testing::internal::GetCapturedStdout();
    EXPECT_TRUE(output.find("test info") == std::string::npos);
    EXPECT_TRUE(output.find("test warn") != std::string::npos);
    unsetenv("KRR_LOG_LEVEL");
}

TEST_F(LoggerTests, LogWithFilterIgnoreEnvironment) {
    setenv("KRR_LOG_LEVEL", "error", 1);
    LoggerBuilder{}.to_console(true).filter_level(spdlog::level::warn).init();
    ::testing::internal::CaptureStdout();
    LogInfo("test info");
    LogFlush();
    LogWarn("test warn");
    LogFlush();
    auto const &output = ::testing::internal::GetCapturedStdout();
    EXPECT_TRUE(output.find("test info") == std::string::npos);
    EXPECT_TRUE(output.find("test warn") != std::string::npos);
    unsetenv("KRR_LOG_LEVEL");
}
#endif
