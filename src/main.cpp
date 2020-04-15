#include "core/VulkanApp.h"
#include "core/VulkanFunctions.h"
#include "os/Common.h"
#include "os/Window.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

class Sample : public Core::VulkanApp
{
public:
  Sample(std::ostream& debugStream) :
    VulkanApp(debugStream, true),
    m_Window(Os::Window(*this)),
    m_RenderFinishedFence(nullptr)
  {}

  virtual ~Sample()
  {
    Core::vkDeviceWaitIdle(m_VulkanParameters.m_Device);
    if (m_NextImageAvailableSemaphore) {
      Core::vkDestroySemaphore(m_VulkanParameters.m_Device, m_NextImageAvailableSemaphore, nullptr);
      m_NextImageAvailableSemaphore = nullptr;
    }
    if (m_RenderFinishedSemaphore) {
      Core::vkDestroySemaphore(m_VulkanParameters.m_Device, m_RenderFinishedSemaphore, nullptr);
      m_RenderFinishedSemaphore = nullptr;
    }
    if (m_RenderFinishedFence) {
      Core::vkDestroyFence(m_VulkanParameters.m_Device, m_RenderFinishedFence, nullptr);
      m_RenderFinishedFence = nullptr;
    }
  }

  bool Initialize()
  {
    if (!m_Window.Create(L"Hello Vulkan!")) {
      return false;
    }

    if (!PrepareVulkan(m_Window.GetWindowParameters())) {
      return false;
    }

    if (!CreateSwapchainAndRenderResources()) {
      return false;
    }

    if (!CreateSemaphoresAndFences()) {
      return false;
    }

    return true;
  }

  [[nodiscard]] bool StartMainLoop() const { return m_Window.RenderLoop(); }

  bool Render() override
  {
    uint32_t acquiredImageIdx;
    VkResult result = Core::vkAcquireNextImageKHR(m_VulkanParameters.m_Device,
                                                  m_VulkanParameters.m_Swapchain.m_Handle,
                                                  UINT64_MAX,
                                                  m_NextImageAvailableSemaphore,
                                                  nullptr,
                                                  &acquiredImageIdx);

    switch (result) {
    case VK_SUCCESS:
    case VK_SUBOPTIMAL_KHR:
      break;
    case VK_ERROR_OUT_OF_DATE_KHR:
#ifdef _DEBUG
      m_DebugOutput << "Swapchain image out of date during acquiring, recreating the swapchain" << std::endl;
#endif
      Core::vkDeviceWaitIdle(m_VulkanParameters.m_Device);
      Core::vkResetFences(m_VulkanParameters.m_Device, 1, &m_RenderFinishedFence);
      return RecreateSwapchainAndRenderResources();

    default:
      std::cerr << "Render error! (" << result << ")" << std::endl;
      return false;
    }

    VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                nullptr,
                                1,
                                &m_NextImageAvailableSemaphore,
                                &waitStageMask,
                                1,
                                &m_VulkanParameters.m_PresentCommandBuffers[acquiredImageIdx],
                                1,
                                &m_RenderFinishedSemaphore };

    result = Core::vkQueueSubmit(m_VulkanParameters.m_Queue, 1, &submitInfo, m_RenderFinishedFence);
    if (result != VK_SUCCESS) {
      std::cerr << "Error while submitting commands to the present queue" << std::endl;
      return false;
    }

    VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                     nullptr,
                                     1,
                                     &m_RenderFinishedSemaphore,
                                     1,
                                     &m_VulkanParameters.m_Swapchain.m_Handle,
                                     &acquiredImageIdx,
                                     nullptr };

    result = Core::vkQueuePresentKHR(m_VulkanParameters.m_Queue, &presentInfo);
    switch (result) {
    case VK_SUCCESS:
      break;
    case VK_ERROR_OUT_OF_DATE_KHR:
    case VK_SUBOPTIMAL_KHR:
#ifdef _DEBUG
      m_DebugOutput << "Swapchain image suboptimal or out of date during presenting, recreating swapchain" << std::endl;
#endif
      Core::vkDeviceWaitIdle(m_VulkanParameters.m_Device);
      Core::vkResetFences(m_VulkanParameters.m_Device, 1, &m_RenderFinishedFence);
      return RecreateSwapchainAndRenderResources();
    default:
      std::cerr << "Render error! (" << result << ")" << std::endl;
      return false;
    }

    result = Core::vkWaitForFences(m_VulkanParameters.m_Device, 1, &m_RenderFinishedFence, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
      std::cout << "Fence waiting error" << result << std::endl;
      return false;
    }

    if (Core::vkResetFences(m_VulkanParameters.m_Device, 1, &m_RenderFinishedFence) != VK_SUCCESS) {
      std::cout << "Could not reset fence" << result << std::endl;
      return false;
    }
    return true;
  }

  [[nodiscard]] bool CanRender() const override { return true; }

private:
  bool CreateSemaphoresAndFences()
  {
    VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0 };

    VkResult result =
      Core::vkCreateFence(m_VulkanParameters.m_Device, &fenceCreateInfo, nullptr, &m_RenderFinishedFence);

    if (result != VK_SUCCESS) {
      std::cerr << "Could not create the fence!" << result << std::endl;
      return false;
    }

    VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };

    result = Core::vkCreateSemaphore(
      m_VulkanParameters.m_Device, &semaphoreCreateInfo, nullptr, &m_NextImageAvailableSemaphore);
    if (result != VK_SUCCESS) {
      std::cerr << "Could not create the semaphore for the next image available: " << result << std::endl;
      return false;
    }

    result =
      Core::vkCreateSemaphore(m_VulkanParameters.m_Device, &semaphoreCreateInfo, nullptr, &m_RenderFinishedSemaphore);
    if (result != VK_SUCCESS) {
      std::cerr << "Could not create the semaphore for the next image available: " << result << std::endl;
      return false;
    }
    return true;
  }

  Os::Window m_Window;
  VkFence m_RenderFinishedFence;
  VkSemaphore m_NextImageAvailableSemaphore;
  VkSemaphore m_RenderFinishedSemaphore;
};

int main()
{
  std::filesystem::path const debugFilePath = Os::GetExecutableDirectory() / "debug.txt";
  std::ofstream debugFile(debugFilePath);

  if (!debugFile.is_open()) {
    std::cerr << "Cant create debug file: " << debugFilePath.c_str() << std::endl;
    return 1;
  }

  Sample app(debugFile);

  if (!app.Initialize()) {
    debugFile.close();
    return 1;
  }

  if (!app.StartMainLoop()) {
    debugFile.close();
    return 1;
  }

  debugFile.close();
  return 0;
}