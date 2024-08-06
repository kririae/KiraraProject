#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "kira/Logger.h"

using namespace kira;

class LoggerTests : public ::testing::Test {
protected:
  void SetUp() override {
    if (std::filesystem::exists("test.log"))
      std::filesystem::remove("test.log");
    spdlog::shutdown();
  }

  void TearDown() override {
    if (std::filesystem::exists("test.log"))
      std::filesystem::remove("test.log");
  }

  bool FileContainsLog(std::string const &filename,
                       std::string const &logMessage) {
    std::ifstream file(filename);
    std::string line;
    while (std::getline(file, line))
      if (line.find(logMessage) != std::string::npos)
        return true;
    return false;
  }
};

TEST_F(LoggerTests, SetupLoggerConsoleOnly) {
  SetupLogger(true, std::nullopt);

  // Create the logger
  auto *logger = GetLogger<"testLogger">();
  spdlog::shutdown();
  auto *logger2 = spdlog::get("testLogger").get();
  EXPECT_EQ(logger, logger2);
}
