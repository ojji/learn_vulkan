#include "Window.h"
#include <chrono>
#include <iostream>
#include <thread>

namespace Os {
Window::Window(Core::VulkanApp& vulkanApp) : m_WindowParameters(WindowParameters()), m_VulkanApp(vulkanApp)
{}

Window::~Window()
{
  m_WindowParameters.m_Handle = nullptr;
}

#ifdef VK_USE_PLATFORM_WIN32_KHR
constexpr wchar_t WINDOW_CLASS_NAME[] = L"Learn_Vulkan";
LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg) {
  case WM_DESTROY: {
    PostQuitMessage(0);
    return 0;
  }
  default:
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }
}
#endif

bool Window::Create(wchar_t const windowTitle[])
{
#ifdef VK_USE_PLATFORM_WIN32_KHR
  m_WindowParameters.m_Instance = GetModuleHandle(nullptr);
  WNDCLASSEX wc = {};
  wc.cbSize = sizeof(WNDCLASSEX);
  wc.lpszClassName = WINDOW_CLASS_NAME;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = m_WindowParameters.m_Instance;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = 0;
  wc.hIcon = nullptr;
  wc.hbrBackground = 0;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.hIconSm = nullptr;
  wc.lpszMenuName = nullptr;

  if (!RegisterClassEx(&wc)) {
    return false;
  }

  RECT windowRect{ 0, 0, 1280, 720 };
  AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, false);
  m_WindowParameters.m_Handle = CreateWindow(WINDOW_CLASS_NAME,
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
  if (!m_WindowParameters.m_Handle) {
    return false;
  }

  return true;
#endif
}

bool Window::RenderLoop() const
{
#ifdef VK_USE_PLATFORM_WIN32_KHR
  ShowWindow(m_WindowParameters.m_Handle, SW_SHOWNORMAL);
  UpdateWindow(m_WindowParameters.m_Handle);

  MSG msg{};
  volatile bool isRunning = true;

  while (isRunning) {
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      switch (msg.message) {
      case WM_QUIT: {
        isRunning = false;
      } break;
      }
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
    // Draw here if you can
    if (m_VulkanApp.CanRender()) {
      if (!m_VulkanApp.Render()) {
        PostQuitMessage(0);
      }
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
  return true;
#endif
}

WindowParameters Window::GetWindowParameters() const
{
  return m_WindowParameters;
}
} // namespace Os
