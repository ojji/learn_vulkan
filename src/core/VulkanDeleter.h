#ifndef CORE_VULKANDELETER_H
#define CORE_VULKANDELETER_H

#include <iostream>
#include <utility>
#include <vulkan/vulkan.h>

namespace Core {
template<typename T, typename D>
class VulkanDeleter
{
public:
  void swap(VulkanDeleter<T, D>& left, VulkanDeleter<T, D>& right) noexcept
  {
    using std::swap;
    swap(left.m_Object, right.m_Object);
    swap(left.m_DeleterFn, right.m_DeleterFn);
    swap(left.m_Device, right.m_Device);
  }

  VulkanDeleter() : m_Object(VK_NULL_HANDLE), m_DeleterFn(nullptr), m_Device(VK_NULL_HANDLE) {}
  VulkanDeleter(T object, D deleterFn, VkDevice device) : m_Object(object), m_DeleterFn(deleterFn), m_Device(device) {}
  VulkanDeleter(VulkanDeleter const& other) = delete;
  VulkanDeleter(VulkanDeleter&& other) noexcept :
    m_Object(VK_NULL_HANDLE),
    m_DeleterFn(nullptr),
    m_Device(VK_NULL_HANDLE)
  {
    swap(this, other);
  }

  VulkanDeleter& operator=(VulkanDeleter const& other) = delete;
  VulkanDeleter& operator=(VulkanDeleter&& other) noexcept
  {
    if (this != &other) {
      swap(*this, other);
    }

    return *this;
  }

  T& Get() { return m_Object; }

  bool operator!()
  {
    if (m_Object == VK_NULL_HANDLE || m_DeleterFn == nullptr || m_Device == VK_NULL_HANDLE) {
      return true;
    }

    return false;
  }

  ~VulkanDeleter()
  {
    if (m_Object != VK_NULL_HANDLE && m_Device != VK_NULL_HANDLE && m_DeleterFn != nullptr) {
      m_DeleterFn(m_Device, m_Object, nullptr);
    }
  };

private:
  T m_Object;
  D m_DeleterFn;
  VkDevice m_Device;
};
} // namespace Core

#endif // CORE_VULKANDELETER_H
