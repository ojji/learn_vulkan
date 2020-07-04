#pragma once

#ifdef VK_USE_PLATFORM_WIN32_KHR
#include <Windows.h>
#endif

namespace Os {
#ifdef VK_USE_PLATFORM_WIN32_KHR
typedef HMODULE LibraryHandle;
#endif
}
