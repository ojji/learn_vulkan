#ifndef OS_WINDOW_H
#define OS_WINDOW_H

#include <functional>

#ifdef VK_USE_PLATFORM_WIN32_KHR
#include <Windows.h>
#endif
#include "core/VulkanApp.h"

namespace Os {
class Window;

class Window
{
public:
  typedef void (OnWindowCloseCallback)(Window*);
  explicit Window();
  ~Window();
  bool Create(TCHAR const windowTitle[]);
  void PollEvents();
  void SetOnWindowClose(std::function<OnWindowCloseCallback> callback);

  [[nodiscard]]
  WindowParameters GetWindowParameters() const;
  LRESULT HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
  void Window::OnWindowClose(Window* window);

  WindowParameters m_WindowParameters;
  std::function<OnWindowCloseCallback> m_OnWindowClose;
};
}

#endif // OS_WINDOW_H
