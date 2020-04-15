#ifndef OS_COMMON_H
#define OS_COMMON_H

#include <filesystem>

namespace Os
{
  std::filesystem::path GetExecutableDirectory();
} // namespace Os


#endif // OS_COMMON_H