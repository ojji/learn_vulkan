#pragma once

#include "Logger.h"
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <string>
#include <vector>

namespace Utils {
class FileLogger : public ILogger
{
public:
  enum class OpenMode
  {
    Append,
    Truncate
  };

  FileLogger(std::string name,
             std::filesystem::path const& path,
             OpenMode openMode,
             std::initializer_list<std::string> categoriesToLog = {},
             int locationLogWidth = 25);
  FileLogger(std::string name,
             std::filesystem::path const& path,
             OpenMode openMode,
             std::function<bool(LogMessage const&)> filterFn,
             int locationLogWidth = 25);
  ~FileLogger();

  bool ShouldLogMessage(LogMessage const& logMessage) const override;
  void LogDebug(LogMessage const& logMessage) override;
  void LogInfo(LogMessage const& logMessage) override;
  void LogWarning(LogMessage const& logMessage) override;
  void LogError(LogMessage const& logMessage) override;
  void LogCritical(LogMessage const& logMessage) override;

  void MuteCategory(std::string const& category) override;
  void UnmuteCategory(std::string const& category) override;

private:
  FileLogger(std::string name,
             std::filesystem::path const& path,
             OpenMode openMode,
             std::initializer_list<std::string> categoriesToLog,
             std::function<bool(LogMessage const&)> filterFn,
             int locationLogWidth);

  void Log(std::string const& type, LogMessage const& logMessage);
  std::mutex m_CriticalSection;
  std::ofstream m_FileStream;
  std::function<bool(Utils::LogMessage const&)> m_FilterFn;
  int m_LocationLogWidth;
  std::vector<std::string> m_MutedCategories;
};
} // namespace Utils
