#ifndef OS_WINDOW_H
#define OS_WINDOW_H

#ifdef VK_USE_PLATFORM_WIN32_KHR
#include <Windows.h>
#endif  
#include "core/VulkanApp.h"

namespace Os {

struct WindowParameters
{
#ifdef VK_USE_PLATFORM_WIN32_KHR
  HWND m_Handle;
  HINSTANCE m_Instance;
  WindowParameters() : m_Handle(), m_Instance(){ }
#endif  
};

class Window
{
public:
  explicit Window(Core::VulkanApp& vulkanApp);
  ~Window();
  bool Create(wchar_t const windowTitle[]);

  [[nodiscard]]
  bool RenderLoop() const;
  [[nodiscard]]
  WindowParameters GetWindowParameters() const;
private:
  WindowParameters m_WindowParameters;
  Core::VulkanApp& m_VulkanApp;
};
}

#endif // OS_WINDOW_H