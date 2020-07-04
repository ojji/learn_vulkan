#pragma once

#include <array>
#include <cmath>

namespace Core {
class Transition
{
public:
  Transition(){};
  Transition(std::array<float, 4> firstColor, std::array<float, 4> secondColor, float periodInSeconds) :
    m_FirstColor(firstColor),
    m_SecondColor(secondColor),
    m_PeriodInSeconds(periodInSeconds)
  {}

  std::array<float, 4> GetValue(float timeElapsedInMs)
  {
    float const pi = std::atanf(1) * 4.0f;
    float remainder = std::fmod(timeElapsedInMs, m_PeriodInSeconds * 2.0f * 1000.0f);
    float x = 0.5f * sinf((pi * remainder * (1.0f / (m_PeriodInSeconds * 1000.0f))) - pi * 0.5f) + 0.5f;
    float r = m_FirstColor[0] + x * (m_SecondColor[0] - m_FirstColor[0]);
    float g = m_FirstColor[1] + x * (m_SecondColor[1] - m_FirstColor[1]);
    float b = m_FirstColor[2] + x * (m_SecondColor[2] - m_FirstColor[2]);
    float a = m_FirstColor[3] + x * (m_SecondColor[3] - m_FirstColor[3]);
    return std::array<float, 4>({ r, g, b, a });
  }

private:
  std::array<float, 4> m_FirstColor;
  std::array<float, 4> m_SecondColor;
  float m_PeriodInSeconds;
};
} // namespace Core
