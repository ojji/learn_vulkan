#include "FileLogger.h"
#include <algorithm>
#include <iostream>
#include <sstream>

namespace Utils {
FileLogger::FileLogger(std::string name,
                       std::filesystem::path const& path,
                       OpenMode openMode,
                       std::initializer_list<std::string> categoriesToLog,
                       int locationLogWidth) :
  FileLogger(std::move(name), path, openMode, categoriesToLog, nullptr, locationLogWidth)
{}

FileLogger::FileLogger(std::string name,
                       std::filesystem::path const& path,
                       OpenMode openMode,
                       std::function<bool(LogMessage const&)> filterFn,
                       int locationLogWidth) :
  FileLogger(std::move(name), path, openMode, std::initializer_list<std::string>{}, filterFn, locationLogWidth)
{}

FileLogger::FileLogger(std::string name,
                       std::filesystem::path const& path,
                       OpenMode openMode,
                       std::initializer_list<std::string> categoriesToLog,
                       std::function<bool(LogMessage const&)> filterFn,
                       int locationLogWidth) :
  ILogger(std::move(name)),
  m_CriticalSection(std::mutex()),
  m_FilterFn(filterFn),
  m_LocationLogWidth(locationLogWidth),
  m_MutedCategories(std::vector<std::string>())
{
  m_FileStream = std::ofstream();
  if (openMode == OpenMode::Append) {
    m_FileStream.open(path.c_str(), std::ios::app | std::ios::out);
  } else {
    m_FileStream.open(path.c_str(), std::ios::trunc | std::ios::out);
  }

  if (!m_FileStream.is_open()) { throw std::runtime_error("Could not open file " + path.string()); }

  if (m_FilterFn == nullptr) {
    std::vector<std::string> categories = std::vector<std::string>(categoriesToLog.begin(), categoriesToLog.end());
    m_FilterFn = [categories](LogMessage const& logMessage) -> bool {
      if (categories.size() == 0) { return true; }
      auto it = std::find(categories.begin(), categories.end(), logMessage.Category);
      return it != categories.end();
    };
  }
}

FileLogger::~FileLogger()
{
  if (m_FileStream) {
    m_FileStream.flush();
    m_FileStream.close();
  }
}

bool FileLogger::ShouldLogMessage(LogMessage const& message) const
{
  auto it = std::find(m_MutedCategories.cbegin(), m_MutedCategories.cend(), message.Category);
  if (it == m_MutedCategories.cend()) { return m_FilterFn(message); }
  return false;
}

void FileLogger::LogDebug(LogMessage const& logMessage)
{
  Log("DEBUG", logMessage);
}

void FileLogger::LogInfo(LogMessage const& logMessage)
{
  Log("INFO", logMessage);
}

void FileLogger::LogWarning(LogMessage const& logMessage)
{
  Log("WARNING", logMessage);
}

void FileLogger::LogError(LogMessage const& logMessage)
{
  Log("ERROR", logMessage);
}

void FileLogger::LogCritical(LogMessage const& logMessage)
{
  Log("CRITICAL", logMessage);
}

void FileLogger::MuteCategory(std::string const& category)
{
  m_MutedCategories.push_back(category);
}

void FileLogger::UnmuteCategory(std::string const& category)
{
  auto it = std::find(m_MutedCategories.begin(), m_MutedCategories.end(), category);
  if (it != m_MutedCategories.end()) { m_MutedCategories.erase(it); }
}

void FileLogger::Log(std::string const& type, LogMessage const& logMessage)
{
  std::lock_guard<std::mutex> lock(m_CriticalSection);
  auto now = std::chrono::system_clock::now();
  auto time = std::chrono::system_clock::to_time_t(now);

  tm buf;
  localtime_s(&buf, &time);
  std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

  char oldFill = m_FileStream.fill('0');
  m_FileStream << "[";
  m_FileStream << std::put_time(&buf, "%T") << "," << std::setw(3) << ms.count();
  m_FileStream << "] " << std::setfill(oldFill);
  m_FileStream << "[";
  m_FileStream << logMessage.Category;
  m_FileStream << "] ";

  if (!logMessage.File.empty() && !logMessage.Func.empty() && logMessage.Line != LogMessage::DefaultLineValue) {
    std::ostringstream lineMessage;
    lineMessage << "[";
    lineMessage << logMessage.File;
    lineMessage << ", ";
    lineMessage << logMessage.Func << "()";
    lineMessage << ":";
    lineMessage << logMessage.Line;
    lineMessage << "] ";
    m_FileStream << std::left << std::setw(m_LocationLogWidth) << lineMessage.str() << std::right;
  }

  m_FileStream << std::setw(8) << type << ": ";
  if (!logMessage.Message.empty()) {
    m_FileStream << logMessage.Message << std::endl;
  } else {
    m_FileStream << std::endl;
  }
  if (!logMessage.LongMessage.empty()) { m_FileStream << logMessage.LongMessage << std::endl; }
}
} // namespace Utils
