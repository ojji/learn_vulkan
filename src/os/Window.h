#pragma once

#include <functional>

#ifdef VK_USE_PLATFORM_WIN32_KHR
#include <Windows.h>
#endif

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
  typedef void (OnWindowCloseCallback)(Window*);
  explicit Window();
  ~Window();
  bool Create(wchar_t const windowTitle[], uint32_t width, uint32_t height);
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
