#include <gtest/gtest.h>

#include <fstream>
#include <random>

#include "kira/Anyhow.h"
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
    }

    void TearDown() override {
        if (std::filesystem::exists(tempLogPath))
            std::filesystem::remove(tempLogPath);
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
    SetupLogger("testSetup", true, std::nullopt);
    auto logger = spdlog::get("testSetup");
    EXPECT_NE(logger, nullptr);
    EXPECT_EQ(logger->sinks().size(), 1);

    SetupLogger("testSetupFile1", true, tempLogPath);
    logger = spdlog::get("testSetupFile1");
    EXPECT_NE(logger, nullptr);
    EXPECT_EQ(logger->sinks().size(), 2);

    SetupLogger("testSetupFile2", true, tempLogPath);
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
    SetupLogger("testDuplicate", true, std::nullopt);
    EXPECT_THROW(SetupLogger("testDuplicate", true, std::nullopt), Anyhow);
}

TEST_F(LoggerTests, LogToConsole) {
    SetupLogger(defaultLoggerName, true, std::nullopt);
    ::testing::internal::CaptureStdout();
    LogInfo("test message");
    LogFlush();
    auto const &output = ::testing::internal::GetCapturedStdout();
    EXPECT_TRUE(output.find("test message") != std::string::npos);
}

TEST_F(LoggerTests, NoLogToConsole) {
    SetupLogger(defaultLoggerName, false, std::nullopt);
    ::testing::internal::CaptureStdout();
    LogInfo("test message");
    LogFlush();
    auto const &output = ::testing::internal::GetCapturedStdout();
    EXPECT_TRUE(output.find("test message") == std::string::npos);
}

TEST_F(LoggerTests, LogToFileAndConsole) {
    SetupLogger(defaultLoggerName, true, tempLogPath);
    EXPECT_NE(spdlog::get(defaultLoggerName.value).get(), nullptr);

    ::testing::internal::CaptureStdout();
    LogInfo("test message");
    LogFlush();
    auto const &output = ::testing::internal::GetCapturedStdout();
    EXPECT_TRUE(output.find("test message") != std::string::npos);
    EXPECT_TRUE(FileContainsLog("test message"));
}
