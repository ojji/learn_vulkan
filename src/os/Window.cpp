#include "Window.h"
#include "core/Input.h"
#include "utils/Logger.h"
#include <Windows.h>
#include <cassert>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

namespace Os {

#ifdef VK_USE_PLATFORM_WIN32_KHR
constexpr wchar_t WINDOW_CLASS_NAME[] = L"Learn_Vulkan";
LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  Window* window = reinterpret_cast<Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

  if (!window) { return DefWindowProcW(hwnd, uMsg, wParam, lParam); }

  return window->HandleMessage(hwnd, uMsg, wParam, lParam);
}
#endif

Window::Window() :
  m_WindowParameters(WindowParameters()),
  m_OnWindowClose(nullptr),
  m_OnCharacterReceived(nullptr),
  m_OnKeyEvent(nullptr),
  m_LastHighSurrogate(0)
{}

Window::~Window()
{
  m_WindowParameters.m_Handle = nullptr;
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

void Window::SetOnCharacterReceived(std::function<OnCharacterReceivedCallback> callback)
{
  m_OnCharacterReceived = callback;
}

void Window::SetOnKeyEvent(std::function<OnKeyEventCallback> callback)
{
  m_OnKeyEvent = callback;
}

WindowParameters Window::GetWindowParameters() const
{
  return m_WindowParameters;
}

LRESULT Window::HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg) {
  // Character events
  case WM_CHAR:
  case WM_SYSCHAR: {
    uint16_t key = static_cast<uint16_t>(wParam);
    uint32_t codePoint;

    // if the character is a high surrogate/low surrogate pair
    if (0xD800 <= key && key <= 0xDBFF) {
      assert(m_LastHighSurrogate == 0);
      m_LastHighSurrogate = key;
      return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    } else if (0xDC00 <= key && key <= 0xDFFF) {
      assert(0xD800 <= m_LastHighSurrogate && m_LastHighSurrogate <= 0xDBFF);
      // see: https://en.wikipedia.org/wiki/UTF-16#Code_points_from_U+010000_to_U+10FFFF
      codePoint = 0x10000;
      codePoint += (m_LastHighSurrogate - 0xD800) << 10;
      codePoint += (key - 0xDC00);
      m_LastHighSurrogate = 0;
    } else {
      codePoint = static_cast<uint32_t>(key);
    }

    if (m_OnCharacterReceived) { m_OnCharacterReceived(this, codePoint, ReadModifiers()); }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
  } break;
  // Keyboard events
  case WM_KEYDOWN:
  case WM_KEYUP:
  case WM_SYSKEYDOWN:
  case WM_SYSKEYUP: {
    uint8_t key = static_cast<uint8_t>(wParam);
    Core::KeypressAction action =
      ((HIWORD(lParam) & KF_UP) != 0) ? Core::KeypressAction::Released : Core::KeypressAction::Pressed;
    uint16_t repeatCount = static_cast<uint16_t>(lParam & 0xFFFF);
    Core::ModifierKeys modifiers = ReadModifiers();

    if (wParam == VK_CONTROL) {
      // Handling the AltGr key is special on Windows because the OS generates two messages: a VK_CONTROL message first
      // and a VK_MENU second at the same time. We ignore the first VK_CONTROL key and process the VK_MENU key.
      DWORD msgTime = GetMessageTime();
      MSG next;
      if (PeekMessageW(&next, NULL, 0, 0, PM_NOREMOVE)) {
        if ((next.message == WM_KEYDOWN || next.message == WM_KEYUP || next.message == WM_SYSKEYDOWN
             || next.message == WM_SYSKEYUP)) {
          if (next.time == msgTime && ((HIWORD(next.lParam) & KF_EXTENDED) != 0) && next.wParam == VK_MENU) {
            return DefWindowProcW(hwnd, uMsg, wParam, lParam);
          }
        }
      }
    } else if (wParam == VK_SNAPSHOT) {
      // The print screen key only generates a released event, manually call the events with both a pressed and a
      // released action
      if (m_OnKeyEvent) {
        m_OnKeyEvent(this, key, Core::KeypressAction::Pressed, modifiers, repeatCount);
        m_OnKeyEvent(this, key, Core::KeypressAction::Released, modifiers, repeatCount);
      }
      return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    if (m_OnKeyEvent) { m_OnKeyEvent(this, key, action, modifiers, repeatCount); }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
  } break;
  case WM_SYSCOMMAND: {
    // pressing the alt key for the menu, simply ignore the message
    if (wParam == SC_KEYMENU) { return 0; }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
  } break;
  case WM_DESTROY: {
    PostQuitMessage(0);
    return 0;
  } break;
  default: {
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
  } break;
  }
}

void Window::OnWindowClose(Window* window)
{
  if (m_OnWindowClose) { m_OnWindowClose(window); }
}

Core::ModifierKeys Window::ReadModifiers()
{
  Core::ModifierKeys modifiers;
  if (GetKeyState(VK_LCONTROL) & 0x8000) { modifiers |= Core::ModifierKeyBits::LeftControl; }
  if (GetKeyState(VK_RCONTROL) & 0x8000) { modifiers |= Core::ModifierKeyBits::RightControl; }
  if (GetKeyState(VK_LSHIFT) & 0x8000) { modifiers |= Core::ModifierKeyBits::LeftShift; }
  if (GetKeyState(VK_RSHIFT) & 0x8000) { modifiers |= Core::ModifierKeyBits::RightShift; }
  if (GetKeyState(VK_LMENU) & 0x8000) { modifiers |= Core::ModifierKeyBits::Alt; }
  if (GetKeyState(VK_RMENU) & 0x8000) { modifiers |= Core::ModifierKeyBits::AltGr; }
  if (GetKeyState(VK_LWIN) & 0x8000) { modifiers |= Core::ModifierKeyBits::Windows; }
  if (GetKeyState(VK_RWIN) & 0x8000) { modifiers |= Core::ModifierKeyBits::Windows; }
  if (GetKeyState(VK_APPS) & 0x8000) { modifiers |= Core::ModifierKeyBits::Application; }
  if (GetKeyState(VK_CAPITAL) & 0x1) { modifiers |= Core::ModifierKeyBits::CapsLock; }
  if (GetKeyState(VK_NUMLOCK) & 0x1) { modifiers |= Core::ModifierKeyBits::NumLock; }
  if (GetKeyState(VK_SCROLL) & 0x1) { modifiers |= Core::ModifierKeyBits::ScrollLock; }
  return modifiers;
}
} // namespace Os
