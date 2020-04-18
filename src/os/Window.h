#ifndef OS_WINDOW_H
#define OS_WINDOW_H

#ifdef VK_USE_PLATFORM_WIN32_KHR
#include <Windows.h>
#endif  
#include "core/VulkanApp.h"

namespace Os {
class Window;

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
