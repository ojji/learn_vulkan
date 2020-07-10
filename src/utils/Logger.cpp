#include "Logger.h"

namespace Utils {

void Logger::LogDebug(std::string message, std::string category, std::string longMessage)
{
  LogMessage logMessage;
  logMessage.Category = std::move(category);
  logMessage.Message = std::move(message);
  logMessage.LongMessage = std::move(longMessage);

  for (auto const& logger : m_Loggers) {
    if (logger->ShouldLogMessage(logMessage)) { logger->LogDebug(logMessage); }
  }
}

void Logger::LogDebugEx(
  std::string message, std::string category, std::string file, std::string func, int line, std::string longMessage)
{
  LogMessage logMessage;
  logMessage.Category = std::move(category);
  logMessage.File = std::move(file);
  logMessage.Func = std::move(func);
  logMessage.Line = line;
  logMessage.Message = std::move(message);
  logMessage.LongMessage = std::move(longMessage);

  for (auto const& logger : m_Loggers) {
    if (logger->ShouldLogMessage(logMessage)) { logger->LogDebug(logMessage); }
  }
}

void Logger::LogInfo(std::string message, std::string category, std::string longMessage)
{
  LogMessage logMessage;
  logMessage.Category = std::move(category);
  logMessage.Message = std::move(message);
  logMessage.LongMessage = std::move(longMessage);

  for (auto const& logger : m_Loggers) {
    if (logger->ShouldLogMessage(logMessage)) { logger->LogInfo(logMessage); }
  }
}

void Logger::LogInfoEx(
  std::string message, std::string category, std::string file, std::string func, int line, std::string longMessage)
{
  LogMessage logMessage;
  logMessage.Category = std::move(category);
  logMessage.File = std::move(file);
  logMessage.Func = std::move(func);
  logMessage.Line = line;
  logMessage.Message = std::move(message);
  logMessage.LongMessage = std::move(longMessage);

  for (auto const& logger : m_Loggers) {
    if (logger->ShouldLogMessage(logMessage)) { logger->LogInfo(logMessage); }
  }
}

void Logger::LogWarning(std::string message, std::string category, std::string longMessage)
{
  LogMessage logMessage;
  logMessage.Category = std::move(category);
  logMessage.Message = std::move(message);
  logMessage.LongMessage = std::move(longMessage);

  for (auto const& logger : m_Loggers) {
    if (logger->ShouldLogMessage(logMessage)) { logger->LogWarning(logMessage); }
  }
}

void Logger::LogWarningEx(
  std::string message, std::string category, std::string file, std::string func, int line, std::string longMessage)
{
  LogMessage logMessage;
  logMessage.Category = std::move(category);
  logMessage.File = std::move(file);
  logMessage.Func = std::move(func);
  logMessage.Line = line;
  logMessage.Message = std::move(message);
  logMessage.LongMessage = std::move(longMessage);

  for (auto const& logger : m_Loggers) {
    if (logger->ShouldLogMessage(logMessage)) { logger->LogWarning(logMessage); }
  }
}

void Logger::LogError(std::string message, std::string category, std::string longMessage)
{
  LogMessage logMessage;
  logMessage.Category = std::move(category);
  logMessage.Message = std::move(message);
  logMessage.LongMessage = std::move(longMessage);

  for (auto const& logger : m_Loggers) {
    if (logger->ShouldLogMessage(logMessage)) { logger->LogError(logMessage); }
  }
}

void Logger::LogErrorEx(
  std::string message, std::string category, std::string file, std::string func, int line, std::string longMessage)
{
  LogMessage logMessage;
  logMessage.Category = std::move(category);
  logMessage.File = std::move(file);
  logMessage.Func = std::move(func);
  logMessage.Line = line;
  logMessage.Message = std::move(message);
  logMessage.LongMessage = std::move(longMessage);

  for (auto const& logger : m_Loggers) {
    if (logger->ShouldLogMessage(logMessage)) { logger->LogError(logMessage); }
  }
}

void Logger::LogCritical(std::string message, std::string category, std::string longMessage)
{
  LogMessage logMessage;
  logMessage.Category = std::move(category);
  logMessage.Message = std::move(message);
  logMessage.LongMessage = std::move(longMessage);

  for (auto const& logger : m_Loggers) {
    if (logger->ShouldLogMessage(logMessage)) { logger->LogCritical(logMessage); }
  }
}

void Logger::LogCriticalEx(
  std::string message, std::string category, std::string file, std::string func, int line, std::string longMessage)
{
  LogMessage logMessage;
  logMessage.Category = std::move(category);
  logMessage.File = std::move(file);
  logMessage.Func = std::move(func);
  logMessage.Line = line;
  logMessage.Message = std::move(message);
  logMessage.LongMessage = std::move(longMessage);

  for (auto const& logger : m_Loggers) {
    if (logger->ShouldLogMessage(logMessage)) { logger->LogCritical(logMessage); }
  }
}

} // namespace Utils
