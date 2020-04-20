#include "core/VulkanApp.h"
#include "core/VulkanFunctions.h"
#include "os/Common.h"
#include "os/Window.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <tchar.h>
#include <thread>

class Sample : public Core::VulkanApp
{
public:
  Sample(std::ostream& debugStream) : VulkanApp(debugStream, true), m_Window(), m_CurrentResourceIdx(0) {}

  virtual ~Sample()
  {
    Core::vkDeviceWaitIdle(m_VulkanParameters.m_Device);
    FreeVertexBuffer(m_VertexBuffer);
    for (uint32_t i = 0; i != m_FrameResources.size(); ++i) {
      FreeFrameResource(m_FrameResources[i]);
    }
  }

  bool Initialize()
  {
    if (!m_Window.Create(_T("Hello Vulkan!"))) {
      return false;
    }

    if (!PrepareVulkan(m_Window.GetWindowParameters())) {
      return false;
    }

    if (!CreateCommandPool()) {
      return false;
    }

    if (!CreateSwapchain()) {
      return false;
    }

    if (!CreateRenderPass()) {
      return false;
    }

    if (!CreatePipeline()) {
      return false;
    }

    if (!CreateFrameResources(3, m_FrameResources)) {
      return false;
    }

    std::vector<Core::VertexData> vertices = {
      Core::VertexData{ { -0.7f, 0.7f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
      Core::VertexData{ { 0.7f, 0.7f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
      Core::VertexData{ { -0.7f, -0.7f, 0.0f, 1.0f }, { 1.0f, 0.0f, 1.0f, 1.0f } },
      Core::VertexData{ { 0.7f, -0.7f, 0.0f, 1.0f }, { 0.3f, 0.3f, 0.3f, 1.0f } }
    };

    if (!CreateVertexBuffer(vertices, m_VertexBuffer)) {
      return false;
    }

    return true;
  }

  static DWORD WINAPI RenderLoop(LPVOID param)
  {
    Sample* app = reinterpret_cast<Sample*>(param);
    while (app->m_IsRunning) {
      // Draw here if you can
      if (app->CanRender()) {
        if (!app->Render()) {
          PostQuitMessage(0);
        }
      }
    }
    return 0;
  }

  void OnWindowClose(Os::Window* window)
  {
    UNREFERENCED_PARAMETER(window);
    m_IsRunning = false;
  }

  [[nodiscard]] bool StartMainLoop()
  {
    auto wrapper = std::bind(&Sample::OnWindowClose, this, std::placeholders::_1);
    m_Window.SetOnWindowClose(wrapper);
#ifdef VK_USE_PLATFORM_WIN32_KHR
    HANDLE renderThread = CreateThread(NULL, 0, Sample::RenderLoop, reinterpret_cast<void*>(this), 0, NULL);
    m_IsRunning = true;

    while (m_IsRunning) {
      m_Window.PollEvents();
      _mm_pause();
    }

    WaitForSingleObject(renderThread, INFINITE);
    return true;
#endif
  }

  bool Render() override
  {
    uint32_t currentResourceIdx = (InterlockedIncrement(&m_CurrentResourceIdx) - 1) % 3;
    VkResult result = Core::vkWaitForFences(m_VulkanParameters.m_Device,
                                            1,
                                            &m_FrameResources[currentResourceIdx].m_Fence,
                                            VK_FALSE,
                                            std::numeric_limits<uint64_t>::max());
    if (result != VK_SUCCESS) {
      std::cerr << "Wait on fence timed out" << std::endl;
      return false;
    }

    uint32_t acquiredImageIdx;
    result = Core::vkAcquireNextImageKHR(m_VulkanParameters.m_Device,
                                         m_VulkanParameters.m_Swapchain.m_Handle,
                                         std::numeric_limits<uint64_t>::max(),
                                         m_FrameResources[currentResourceIdx].m_PresentToDrawSemaphore,
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
      return RecreateSwapchain();
    default:
      std::cerr << "Render error! (" << result << ")" << std::endl;
      return false;
    }

    if (!PrepareAndRecordFrame(m_FrameResources[currentResourceIdx].m_CommandBuffer,
                               acquiredImageIdx,
                               m_FrameResources[currentResourceIdx].m_Framebuffer)) {
      return false;
    }

    VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submitInfo = {
      VK_STRUCTURE_TYPE_SUBMIT_INFO, // VkStructureType                sType;
      nullptr,                       // const void*                    pNext;
      1,                             // uint32_t                       waitSemaphoreCount;
      &m_FrameResources[currentResourceIdx].m_PresentToDrawSemaphore, // const VkSemaphore*             pWaitSemaphores;
      &waitStageMask,                                        // const VkPipelineStageFlags*    pWaitDstStageMask;
      1,                                                     // uint32_t                       commandBufferCount;
      &m_FrameResources[currentResourceIdx].m_CommandBuffer, // const VkCommandBuffer*         pCommandBuffers;
      1,                                                     // uint32_t                       signalSemaphoreCount;
      &m_FrameResources[currentResourceIdx].m_DrawToPresentSemaphore // const VkSemaphore* pSignalSemaphores;
    };

    Core::vkResetFences(m_VulkanParameters.m_Device, 1, &m_FrameResources[currentResourceIdx].m_Fence);
    result =
      Core::vkQueueSubmit(m_VulkanParameters.m_Queue, 1, &submitInfo, m_FrameResources[currentResourceIdx].m_Fence);
    if (result != VK_SUCCESS) {
      std::cerr << "Error while submitting commands to the present queue" << std::endl;
      return false;
    }

    VkPresentInfoKHR presentInfo = {
      VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,                             // VkStructureType          sType;
      nullptr,                                                        // const void*              pNext;
      1,                                                              // uint32_t                 waitSemaphoreCount;
      &m_FrameResources[currentResourceIdx].m_DrawToPresentSemaphore, // const VkSemaphore*       pWaitSemaphores;
      1,                                                              // uint32_t                 swapchainCount;
      &m_VulkanParameters.m_Swapchain.m_Handle,                       // const VkSwapchainKHR*    pSwapchains;
      &acquiredImageIdx,                                              // const uint32_t*          pImageIndices;
      nullptr                                                         // VkResult*                pResults;
    };

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
      return RecreateSwapchain();
    default:
      std::cerr << "Render error! (" << result << ")" << std::endl;
      return false;
    }
    return true;
  }

  bool CreateFramebuffer(VkFramebuffer& framebuffer, VkImageView imageView)
  {
    if (framebuffer) {
      Core::vkDestroyFramebuffer(m_VulkanParameters.m_Device, framebuffer, nullptr);
      framebuffer = nullptr;
    }
    VkFramebufferCreateInfo frameBufferCreateInfo = {
      VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,           // VkStructureType             sType;
      nullptr,                                             // const void*                 pNext;
      0,                                                   // VkFramebufferCreateFlags    flags;
      m_VulkanParameters.m_RenderPass,                     // VkRenderPass                renderPass;
      1,                                                   // uint32_t                    attachmentCount;
      &imageView,                                          // const VkImageView*          pAttachments;
      m_VulkanParameters.m_Swapchain.m_ImageExtent.width,  // uint32_t                    width;
      m_VulkanParameters.m_Swapchain.m_ImageExtent.height, // uint32_t                    height;
      1                                                    // uint32_t                    layers;
    };
    VkResult result =
      Core::vkCreateFramebuffer(m_VulkanParameters.m_Device, &frameBufferCreateInfo, nullptr, &framebuffer);
    if (result != VK_SUCCESS) {
      return false;
    }
    return true;
  }

  bool PrepareAndRecordFrame(VkCommandBuffer commandBuffer, uint32_t acquiredImageIdx, VkFramebuffer& framebuffer)
  {
    if (!CreateFramebuffer(framebuffer, m_VulkanParameters.m_Swapchain.m_ImageViews[acquiredImageIdx])) {
      return false;
    }

    VkCommandBufferBeginInfo commandBufferBeginInfo = {
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, // VkStructureType                          sType;
      nullptr,                                     // const void*                              pNext;
      VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, // VkCommandBufferUsageFlags                flags;
      nullptr                                      // const VkCommandBufferInheritanceInfo*    pInheritanceInfo;
    };

    Core::vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo);

    VkImageSubresourceRange subresourceRange = {
      VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT, // VkImageAspectFlags    aspectMask;
      0,                                                // uint32_t              baseMipLevel;
      1,                                                // uint32_t              levelCount;
      0,                                                // uint32_t              baseArrayLayer;
      1                                                 // uint32_t              layerCount;
    };

    VkImageMemoryBarrier fromPresentToDrawBarrier = {
      VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,                    // VkStructureType            sType;
      nullptr,                                                   // const void*                pNext;
      VK_ACCESS_MEMORY_READ_BIT,                                 // VkAccessFlags              srcAccessMask;
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,                      // VkAccessFlags              dstAccessMask;
      VK_IMAGE_LAYOUT_UNDEFINED,                                 // VkImageLayout              oldLayout;
      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,                           // VkImageLayout              newLayout;
      m_VulkanParameters.m_PresentQueueFamilyIdx,                // uint32_t                   srcQueueFamilyIndex;
      m_VulkanParameters.m_PresentQueueFamilyIdx,                // uint32_t                   dstQueueFamilyIndex;
      m_VulkanParameters.m_Swapchain.m_Images[acquiredImageIdx], // VkImage                    image;
      subresourceRange                                           // VkImageSubresourceRange    subresourceRange;
    };

    Core::vkCmdPipelineBarrier(commandBuffer,
                               VkPipelineStageFlagBits::VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                               VkPipelineStageFlagBits::VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                               0,
                               0,
                               nullptr,
                               0,
                               nullptr,
                               1,
                               &fromPresentToDrawBarrier);

    VkClearValue clearValue = { { (85.0f / 255.0f), (87.0f / 255.0f), (112.0f / 255.0f), 0.0f } };
    VkRenderPassBeginInfo renderPassBeginInfo = {
      VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, // VkStructureType        sType;
      nullptr,                                  // const void*            pNext;
      m_VulkanParameters.m_RenderPass,          // VkRenderPass           renderPass;
      framebuffer,                              // VkFramebuffer          framebuffer;
      { { 0, 0 },
        { m_VulkanParameters.m_Swapchain.m_ImageExtent.width,
          m_VulkanParameters.m_Swapchain.m_ImageExtent.height } }, // VkRect2D               renderArea;
      1,                                                           // uint32_t               clearValueCount;
      &clearValue                                                  // const VkClearValue*    pClearValues;
    };

    Core::vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VkSubpassContents::VK_SUBPASS_CONTENTS_INLINE);
    Core::vkCmdBindPipeline(
      commandBuffer, VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS, m_VulkanParameters.m_Pipeline);

    VkViewport viewPort = {
      0.0f,                                                                    // float    x;
      0.0f,                                                                    // float    y;
      static_cast<float>(m_VulkanParameters.m_Swapchain.m_ImageExtent.width),  // float    width;
      static_cast<float>(m_VulkanParameters.m_Swapchain.m_ImageExtent.height), // float    height;
      0.0f,                                                                    // float    minDepth;
      1.0f                                                                     // float    maxDepth;
    };

    Core::vkCmdSetViewport(commandBuffer, 0, 1, &viewPort);

    VkRect2D scissor = {
      { 0, 0 }, // VkOffset2D    offset;
      { m_VulkanParameters.m_Swapchain.m_ImageExtent.width,
        m_VulkanParameters.m_Swapchain.m_ImageExtent.height } // VkExtent2D    extent;
    };
    Core::vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    VkDeviceSize offset = 0;
    Core::vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_VertexBuffer.m_Handle, &offset);
    Core::vkCmdDraw(commandBuffer, 4, 1, 0, 0);

    Core::vkCmdEndRenderPass(commandBuffer);

    VkImageMemoryBarrier fromDrawToPresentBarrier = {
      VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,                    // VkStructureType            sType;
      nullptr,                                                   // const void*                pNext;
      VkAccessFlagBits::VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,    // VkAccessFlags              srcAccessMask;
      VkAccessFlagBits::VK_ACCESS_MEMORY_READ_BIT,               // VkAccessFlags              dstAccessMask;
      VkImageLayout::VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,            // VkImageLayout              oldLayout;
      VkImageLayout::VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,            // VkImageLayout              newLayout;
      m_VulkanParameters.m_PresentQueueFamilyIdx,                // uint32_t                   srcQueueFamilyIndex;
      m_VulkanParameters.m_PresentQueueFamilyIdx,                // uint32_t                   dstQueueFamilyIndex;
      m_VulkanParameters.m_Swapchain.m_Images[acquiredImageIdx], // VkImage                    image;
      subresourceRange                                           // VkImageSubresourceRange    subresourceRange;
    };

    Core::vkCmdPipelineBarrier(commandBuffer,
                               VkPipelineStageFlagBits::VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                               VkPipelineStageFlagBits::VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                               0,
                               0,
                               nullptr,
                               0,
                               nullptr,
                               1,
                               &fromDrawToPresentBarrier);

    Core::vkEndCommandBuffer(commandBuffer);
    return true;
  }

private:
  Os::Window m_Window;
  Core::VertexBuffer m_VertexBuffer;
  volatile bool m_IsRunning;
  std::vector<Core::FrameResource> m_FrameResources;
  volatile uint32_t m_CurrentResourceIdx;
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