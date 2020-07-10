#include "ConsoleLogger.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace Utils {
ConsoleLogger::ConsoleLogger(std::string name, int locationLogWidth) :
  ILogger(std::move(name)),
  m_LocationLogWidth(locationLogWidth),
  m_MutedCategories(std::vector<std::string>())
{}

ConsoleLogger::~ConsoleLogger()
{}

bool ConsoleLogger::ShouldLogMessage([[maybe_unused]] LogMessage const& logMessage) const
{
  auto it = std::find(m_MutedCategories.cbegin(), m_MutedCategories.cend(), logMessage.Category);
  if (it == m_MutedCategories.cend()) { return true; }
  return false;
}

void ConsoleLogger::LogDebug(LogMessage const& logMessage)
{
  Log("DEBUG", logMessage, PlainCyanColor, PlainCyanColor);
}

void ConsoleLogger::LogInfo(LogMessage const& logMessage)
{
  Log("INFO", logMessage, PlainWhiteColor, PlainWhiteColor);
}

void ConsoleLogger::LogWarning(LogMessage const& logMessage)
{
  Log("WARNING", logMessage, BrightYellowColor, BrightYellowColor);
}

void ConsoleLogger::LogError(LogMessage const& logMessage)
{
  Log("ERROR", logMessage, PlainRedColor, PlainRedColor);
}

void ConsoleLogger::LogCritical(LogMessage const& logMessage)
{
  Log("CRITICAL", logMessage, CriticalColor, BrightRedColor);
}

void ConsoleLogger::MuteCategory(std::string const& category)
{
  m_MutedCategories.push_back(category);
}

void ConsoleLogger::UnmuteCategory(std::string const& category)
{
  auto it = std::find(m_MutedCategories.begin(), m_MutedCategories.end(), category);
  if (it != m_MutedCategories.end()) { m_MutedCategories.erase(it); }
}

void ConsoleLogger::Log(std::string const& type,
                        LogMessage const& logMessage,
                        std::string const& messageColor,
                        std::string const& longMessageColor)
{
  std::lock_guard<std::mutex> lock(m_CriticalSection);
  auto now = std::chrono::system_clock::now();
  auto time = std::chrono::system_clock::to_time_t(now);

  tm buf;
  localtime_s(&buf, &time);
  std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

  char oldFill = std::cout.fill('0');
  std::cout << PlainWhiteColor << "[";
  std::cout << BrightBlackColor << std::put_time(&buf, "%T") << "," << std::setw(3) << ms.count();
  std::cout << PlainWhiteColor << "] " << std::setfill(oldFill);
  std::cout << PlainWhiteColor << "[";
  std::cout << BrightWhiteColor << logMessage.Category;
  std::cout << PlainWhiteColor << "] ";

  if (!logMessage.File.empty() && !logMessage.Func.empty() && logMessage.Line != LogMessage::DefaultLineValue) {
    std::ostringstream lineMessage;
    lineMessage << PlainWhiteColor << "[";
    lineMessage << BrightGreenColor << logMessage.File;
    lineMessage << PlainWhiteColor << ", ";
    lineMessage << BrightGreenColor << logMessage.Func << "()";
    lineMessage << PlainWhiteColor << ":";
    lineMessage << BrightCyanColor << logMessage.Line;
    lineMessage << PlainWhiteColor << "] ";
    std::cout << std::left << std::setw(m_LocationLogWidth) << lineMessage.str() << std::right;
  }

  std::cout << std::setw(8) << messageColor << type << ": ";
  if (!logMessage.Message.empty()) {
    std::cout << logMessage.Message << ResetColor << std::endl;
  } else {
    std::cout << ResetColor << std::endl;
  }
  if (!logMessage.LongMessage.empty()) {
    std::cout << longMessageColor << logMessage.LongMessage << ResetColor << std::endl;
  }
}

const std::string ConsoleLogger::PlainWhiteColor = "\x1B[37m";
const std::string ConsoleLogger::BrightWhiteColor = "\x1B[97m";
const std::string ConsoleLogger::BrightGreenColor = "\x1B[92m";
const std::string ConsoleLogger::BrightYellowColor = "\x1B[93m";
const std::string ConsoleLogger::PlainRedColor = "\x1B[31m";
const std::string ConsoleLogger::BrightRedColor = "\x1B[91m";
const std::string ConsoleLogger::BrightCyanColor = "\x1B[96m";
const std::string ConsoleLogger::PlainCyanColor = "\x1B[36m";
const std::string ConsoleLogger::CriticalColor = "\x1B[3;41;97m";
const std::string ConsoleLogger::BrightBlackColor = "\x1B[90m";
const std::string ConsoleLogger::ResetColor = "\033[0m";
} // namespace Utils
