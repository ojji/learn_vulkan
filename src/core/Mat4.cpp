#include "Mat4.h"

namespace Core {
Mat4 Mat4::GetOrthographic(
  float leftPlane, float rightPlane, float topPlane, float bottomPlane, float nearPlane, float farPlane)
{
  Mat4 result;
  result.m_Data = std::array<float, 16>{ 2.0f / (rightPlane - leftPlane),
                                         0.0f,
                                         0.0f,
                                         0.0f,

                                         0.0f,
                                         2.0f / (bottomPlane - topPlane),
                                         0.0f,
                                         0.0f,

                                         0.0f,
                                         0.0f,
                                         1.0f / (nearPlane - farPlane),
                                         0.0f,

                                         -(rightPlane + leftPlane) / (rightPlane - leftPlane),
                                         -(bottomPlane + topPlane) / (bottomPlane - topPlane),
                                         nearPlane / (nearPlane - farPlane),
                                         1.0f };

  return result;
}
} // namespace Core
