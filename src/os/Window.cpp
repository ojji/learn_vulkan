#include "Window.h"
#include "utils/Logger.h"
#include <Windows.h>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

namespace Os {
Window::Window() : m_WindowParameters(WindowParameters())
{}

Window::~Window()
{
  m_WindowParameters.m_Handle = nullptr;
}

#ifdef VK_USE_PLATFORM_WIN32_KHR
constexpr wchar_t WINDOW_CLASS_NAME[] = L"Learn_Vulkan";
LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  Window* window = reinterpret_cast<Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

  if (!window) { return DefWindowProcW(hwnd, uMsg, wParam, lParam); }

  return window->HandleMessage(hwnd, uMsg, wParam, lParam);
}
#endif

LRESULT Window::HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg) {

  // Keyboard events
  case WM_KEYDOWN:
  case WM_KEYUP:
  case WM_SYSKEYDOWN:
  case WM_SYSKEYUP: {
    int repeatCount = lParam & 0xFFFF;
    int key = static_cast<int>(wParam);
    bool isExtended = (HIWORD(lParam) & KF_EXTENDED) != 0;
    bool isAltDown = ((HIWORD(lParam) & KF_ALTDOWN) != 0);
    bool isRepeated = ((HIWORD(lParam) & KF_REPEAT) != 0);
    bool isKeyUp = ((HIWORD(lParam) & KF_UP) != 0);

    std::ostringstream logMessage;

    logMessage << "key: " << std::showbase << std::hex << key << ", repeatCount: " << std::dec << repeatCount
               << ", extended: " << std::boolalpha << isExtended << ", alt down: " << std::boolalpha << isAltDown
               << ", repeated: " << std::boolalpha << isRepeated << ", keyUp: " << std::boolalpha << isKeyUp;
      Utils::Logger::Get().LogDebug(logMessage.str(), "Keyboard");

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
  }

  case WM_DESTROY: {
    PostQuitMessage(0);
    return 0;
  }
  default:
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
  }
}

bool Window::Create(wchar_t const windowTitle[], uint32_t width, uint32_t height)
{
#ifdef VK_USE_PLATFORM_WIN32_KHR
  m_WindowParameters.m_Instance = GetModuleHandleW(nullptr);
  WNDCLASSEX wc = {};
  wc.cbSize = sizeof(WNDCLASSEX);
  wc.lpszClassName = WINDOW_CLASS_NAME;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = m_WindowParameters.m_Instance;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = 0;
  wc.hIcon = nullptr;
  wc.hbrBackground = 0;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.hIconSm = nullptr;
  wc.lpszMenuName = nullptr;

  if (!RegisterClassExW(&wc)) { return false; }

  RECT windowRect{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
  AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, false);
  m_WindowParameters.m_Handle = CreateWindowExW(0L,
                                                WINDOW_CLASS_NAME,
                                                windowTitle,
                                                WS_OVERLAPPEDWINDOW,
                                                CW_USEDEFAULT,
                                                CW_USEDEFAULT,
                                                windowRect.right - windowRect.left,
                                                windowRect.bottom - windowRect.top,
                                                nullptr,
                                                nullptr,
                                                m_WindowParameters.m_Instance,
                                                nullptr);
  if (!m_WindowParameters.m_Handle) { return false; }

  SetWindowLongPtrW(m_WindowParameters.m_Handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  ShowWindow(m_WindowParameters.m_Handle, SW_SHOWNORMAL);
  UpdateWindow(m_WindowParameters.m_Handle);

  return true;
#endif
}

void Window::PollEvents()
{
  MSG msg;
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    switch (msg.message) {
    case WM_QUIT: {
      OnWindowClose(this);
    } break;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

void Window::SetOnWindowClose(std::function<OnWindowCloseCallback> callback)
{
  m_OnWindowClose = callback;
}

WindowParameters Window::GetWindowParameters() const
{
  return m_WindowParameters;
}

void Window::OnWindowClose(Window* window)
{
  if (m_OnWindowClose) { m_OnWindowClose(window); }
}

} // namespace Os
