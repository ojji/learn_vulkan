#include "../Common.h"
#include "utils/Logger.h"
#include <Windows.h>
#include <filesystem>
#include <stdio.h>

namespace Os {
std::filesystem::path GetExecutableDirectory()
{
  constexpr uint32_t MAX_PATH_LENGTH = 32767;
  wchar_t executablePath[MAX_PATH_LENGTH];

  if (!GetModuleFileNameW(NULL, executablePath, static_cast<DWORD>(MAX_PATH_LENGTH))) {
    Utils::Logger::Get().LogErrorEx(
      "Could not retrieve the executable directory", "Filesystem", __FILE__, __func__, __LINE__);
    return std::filesystem::path();
  }

  std::filesystem::path path = std::filesystem::path(executablePath);
  path.remove_filename();
  return path;
}
} // namespace Os
