#pragma once

#include "utils/Flags.h"
#include <stdexcept>
#include <string>

namespace Core {
enum class ModifierKeyBits : uint16_t
{
  LeftShift = 0x1,
  RightShift = 0x2,
  LeftControl = 0x4,
  RightControl = 0x8,
  Alt = 0x10,
  AltGr = 0x20,
  CapsLock = 0x40,
  NumLock = 0x80,
  ScrollLock = 0x100,
  Windows = 0x200,
  Application = 0x400,
  Invalid = 0xF800
};
typedef Utils::Flags<ModifierKeyBits> ModifierKeys;

inline std::string enum_to_string(ModifierKeys value)
{
  if (!value) { return "{}"; }
  std::string result;
  if (value & ModifierKeyBits::LeftShift) { result += "Left Shift | "; }
  if (value & ModifierKeyBits::RightShift) { result += "Right Shift | "; }
  if (value & ModifierKeyBits::LeftControl) { result += "Left Control | "; }
  if (value & ModifierKeyBits::RightControl) { result += "Right Control | "; }
  if (value & ModifierKeyBits::Alt) { result += "Alt | "; }
  if (value & ModifierKeyBits::AltGr) { result += "AltGr | "; }
  if (value & ModifierKeyBits::CapsLock) { result += "Caps Lock | "; }
  if (value & ModifierKeyBits::NumLock) { result += "Num Lock | "; }
  if (value & ModifierKeyBits::ScrollLock) { result += "Scroll Lock | "; }
  if (value & ModifierKeyBits::Windows) { result += "Windows Key | "; }
  if (value & ModifierKeyBits::Application) { result += "Application Key | "; }
  return "{ " + result.substr(0, result.size() - 3) + " }";
};

enum class KeypressAction : uint8_t
{
  Pressed,
  Released
};

inline std::string enum_to_string(KeypressAction state)
{
  switch (state) {
  case KeypressAction::Pressed: {
    return std::string("Pressed");
  } break;
  case KeypressAction::Released: {
    return std::string("Released");
  } break;
  default:
    throw std::runtime_error("Unreachable code");
  }
};

} // namespace Core
