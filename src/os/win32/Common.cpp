#include "../Common.h"
#include <filesystem>
#include <tchar.h>
#include <stdio.h>
#include <Windows.h>

namespace Os
{
std::filesystem::path GetExecutableDirectory()
{
  constexpr uint32_t MAX_PATH_LENGTH = 32767;
  _TCHAR executablePath[MAX_PATH_LENGTH];

  if (!GetModuleFileName(NULL, executablePath, static_cast<DWORD>(MAX_PATH_LENGTH))) {
    _tprintf(_T("Could not retrieve the executable path\n"));
    return std::filesystem::path();
  }

  std::filesystem::path path = std::filesystem::path(executablePath);
  path.remove_filename();
  return path;
}
} // namespace Os
