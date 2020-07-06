#pragma once

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace Utils {

struct LogMessage
{
  LogMessage(){};
  std::string Category;
  std::string File;
  std::string Func;
  int Line = { LogMessage::DefaultLineValue };
  std::string Message;
  std::string LongMessage;
  static constexpr int DefaultLineValue = -1;
};

class ILogger
{
public:
  virtual ~ILogger(){};
  virtual bool ShouldLogMessage(LogMessage const& message) const = 0;
  virtual void LogDebug(LogMessage const& logMessage) = 0;
  virtual void LogInfo(LogMessage const& message) = 0;
  virtual void LogWarning(LogMessage const& message) = 0;
  virtual void LogError(LogMessage const& message) = 0;
  virtual void LogCritical(LogMessage const& message) = 0;

private:
};

class Logger
{
public:
  static Logger& Get()
  {
    static Logger logger;
    return logger;
  }

  template<typename TLogger, typename... TArgs>
  void Register(TArgs&&... args)
  {
    static_assert(std::is_base_of<ILogger, TLogger>::value);
    m_Loggers.emplace_back(std::make_unique<TLogger>(std::forward<TArgs>(args)...));
  };

  void LogDebug(std::string message, std::string category = u8"", std::string longMessage = u8"");
  void LogDebugEx(std::string message,
                  std::string category,
                  std::string file,
                  std::string func,
                  int line,
                  std::string longMessage = u8"");

  void LogInfo(std::string message, std::string category = u8"", std::string longMessage = u8"");
  void LogInfoEx(std::string message,
                 std::string category,
                 std::string file,
                 std::string func,
                 int line,
                 std::string longMessage = u8"");

  void LogWarning(std::string message, std::string category = u8"", std::string longMessage = u8"");
  void LogWarningEx(std::string message,
                    std::string category,
                    std::string file,
                    std::string func,
                    int line,
                    std::string longMessage = u8"");

  void LogError(std::string message, std::string category = u8"", std::string longMessage = u8"");
  void LogErrorEx(std::string message,
                  std::string category,
                  std::string file,
                  std::string func,
                  int line,
                  std::string longMessage = u8"");

  void LogCritical(std::string message, std::string category = u8"", std::string longMessage = u8"");
  void LogCriticalEx(std::string message,
                     std::string category,
                     std::string file,
                     std::string func,
                     int line,
                     std::string longMessage = u8"");

private:
  Logger() = default;
  Logger(Logger const& other) = default;
  Logger(Logger&& other) = default;
  ~Logger() = default;
  Logger& operator=(Logger const& other) = default;
  Logger& operator=(Logger&& other) = default;

  std::vector<std::unique_ptr<ILogger>> m_Loggers;
};

} // namespace Utils
