#ifndef OS_TYPEDEFS_H
#define OS_TYPEDEFS_H

#ifdef VK_USE_PLATFORM_WIN32_KHR
#include <Windows.h>
#endif

namespace Os {
#ifdef VK_USE_PLATFORM_WIN32_KHR
typedef HMODULE LibraryHandle;
#endif

}

#endif // OS_TYPEDEFS_H
