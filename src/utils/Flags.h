#pragma once
#include <type_traits>

namespace Utils {
template<typename T>
class Flags
{
public:
  typedef typename T EnumType;
  typedef typename std::underlying_type<T>::type ValueType;

  Flags() noexcept : m_Value(0) {}

  Flags(T initialValue) noexcept : m_Value(static_cast<ValueType>(initialValue)) {}

  explicit Flags(ValueType value) noexcept : m_Value(value) {}

  Flags(Flags<T> const& other) noexcept : m_Value(other.m_Value) {}

  Flags(Flags<T>&& other) noexcept : m_Value(0) { std::swap(m_Value, other.m_Value); }

  Flags& operator=(Flags<T> const& other)
  {
    m_Value = other.m_Value;
    return *this;
  }

  Flags& operator=(Flags<T>&& other)
  {
    if (this != &other) {
      m_Value = other.m_Value;
      other.m_Value = 0;
    }
    return *this;
  }

  operator bool() const { return m_Value != 0; }

  friend Flags operator&(Flags<T> lhs, Flags<T> const& rhs)
  {
    lhs &= rhs;
    return lhs;
  }

  Flags& operator&=(Flags<T> const& other)
  {
    m_Value &= other.m_Value;
    return *this;
  }

  friend Flags operator|(Flags<T> lhs, Flags<T> const& rhs)
  {
    lhs |= rhs;
    return lhs;
  }

  Flags& operator|=(Flags<T> const& other)
  {
    m_Value |= other.m_Value;
    return *this;
  }

  friend Flags operator^(Flags<T> lhs, Flags<T> const& rhs)
  {
    lhs ^= rhs;
    return lhs;
  }

  Flags& operator^=(Flags<T> const& other)
  {
    m_Value ^= other.m_Value;
    return *this;
  }

private:
  ValueType m_Value;
};

template<typename T>
inline Flags<T> operator|(T const& lhs, T const& rhs) noexcept
{
  return Flags(lhs) | rhs;
}

template<typename T>
inline Flags<T> operator&(T const& lhs, T const& rhs) noexcept
{
  return Flags(lhs) & rhs;
}

template<typename T>
inline Flags<T> operator^(T const& lhs, T const& rhs) noexcept
{
  return Flags(lhs) ^ rhs;
}
} // namespace Utils