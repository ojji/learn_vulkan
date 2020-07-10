#pragma once

#include "core/Input.h"
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
  WindowParameters() : m_Handle(), m_Instance() {}
#endif
};

class Window
{
public:
  typedef void(OnWindowCloseCallback)(Window* window);
  typedef void(OnCharacterReceivedCallback)(Window* window, uint32_t codePoint, Core::ModifierKeys modifiers);
  typedef void(OnKeyEventCallback)(Window* window, uint8_t keyCode, Core::KeypressAction action, Core::ModifierKeys modifiers, uint16_t repeatCount);
  explicit Window();
  ~Window();
  bool Create(wchar_t const windowTitle[], uint32_t width, uint32_t height);
  void PollEvents();
  void SetOnWindowClose(std::function<OnWindowCloseCallback> callback);
  void SetOnCharacterReceived(std::function<OnCharacterReceivedCallback> callback);
  void SetOnKeyEvent(std::function<OnKeyEventCallback> callback);

  [[nodiscard]] WindowParameters GetWindowParameters() const;
  LRESULT HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
  void Window::OnWindowClose(Window* window);
  Core::ModifierKeys ReadModifiers();

  WindowParameters m_WindowParameters;
  std::function<OnWindowCloseCallback> m_OnWindowClose;
  std::function<OnCharacterReceivedCallback> m_OnCharacterReceived;
  std::function<OnKeyEventCallback> m_OnKeyEvent;

  uint32_t m_LastHighSurrogate;
};
} // namespace Os
