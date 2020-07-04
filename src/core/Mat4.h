#pragma once

#include <vulkan/vulkan.hpp>

namespace Core {
class Mat4
{
public:
  static Mat4 GetOrthographic(
    float leftPlane, float rightPlane, float topPlane, float bottomPlane, float nearPlane, float farPlane);

  static constexpr vk::DeviceSize GetSize() { return 16 * sizeof(float); }
  float* GetData() { return m_Data.data(); }

private:
  std::array<float, 16> m_Data;
};
} // namespace Core
