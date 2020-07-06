#pragma once

#include "Logger.h"
#include <mutex>

namespace Utils {
class ConsoleLogger : public ILogger
{
public:
  ConsoleLogger(int locationLogWidth = 25) : m_LocationLogWidth(locationLogWidth){};
  ~ConsoleLogger(){};
  bool ShouldLogMessage([[maybe_unused]] LogMessage const& logMessage) const override { return true; }
  void LogDebug(LogMessage const& logMessage) override;
  void LogInfo(LogMessage const& logMessage) override;
  void LogWarning(LogMessage const& logMessage) override;
  void LogError(LogMessage const& logMessage) override;
  void LogCritical(LogMessage const& logMessage) override;

private:
  void Log(std::string const& type,
           LogMessage const& logMessage,
           std::string const& messageColor,
           std::string const& longMessageColor);

  std::mutex m_CriticalSection;
  int m_LocationLogWidth;

  static const std::string PlainWhiteColor;
  static const std::string BrightWhiteColor;
  static const std::string BrightGreenColor;
  static const std::string BrightYellowColor;
  static const std::string PlainRedColor;
  static const std::string BrightRedColor;
  static const std::string BrightCyanColor;
  static const std::string PlainCyanColor;
  static const std::string CriticalColor;
  static const std::string BrightBlackColor;
  static const std::string ResetColor;
};
} // namespace Utils
