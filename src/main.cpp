#include "core/VulkanFunctions.h"
#include "core/VulkanRenderer.h"
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

class SampleApp
{
public:
  static DWORD WINAPI RenderLoop(LPVOID param)
  {
    SampleApp* app = reinterpret_cast<SampleApp*>(param);
    while (app->m_IsRunning) {
      // Draw here if you can
      if (app->m_VulkanRenderer->CanRender()) {
        if (!app->m_VulkanRenderer->Render()) {
          PostQuitMessage(0);
        }
      }
    }
    return 0;
  }

  SampleApp(std::ostream& debugStream) :
    m_Window(new Os::Window()),
    m_VulkanRenderer(new Core::VulkanRenderer(debugStream))
  {}

  virtual ~SampleApp()
  {
    m_VulkanRenderer->FreeBuffer(m_StagingBuffer);
    m_VulkanRenderer->FreeBuffer(m_VertexBuffer);
  }

  bool Initialize()
  {
    if (!m_Window->Create(_T("Hello Vulkan!"))) {
      return false;
    }

    if (!m_VulkanRenderer->Initialize(m_Window->GetWindowParameters())) {
      return false;
    }

    m_Vertices = { Core::VertexData{ { -0.7f, 0.7f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
                   Core::VertexData{ { 0.7f, 0.7f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
                   Core::VertexData{ { -0.7f, -0.7f, 0.0f, 1.0f }, { 1.0f, 0.0f, 1.0f, 1.0f } },
                   Core::VertexData{ { 0.7f, -0.7f, 0.0f, 1.0f }, { 0.3f, 0.3f, 0.3f, 1.0f } } };


    if (!CreateStagingAndVertexBuffer()) {
      return false;
    }

    return true;
  }

  bool CreateStagingAndVertexBuffer()
  {
    m_StagingBuffer.m_Size = SampleApp::MAX_STAGING_MEMORY_SIZE;
    if (!m_VulkanRenderer->CreateBuffer(m_StagingBuffer,
                                        VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                        VkMemoryPropertyFlagBits::VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
      std::cerr << "Could not create staging buffer" << std::endl;
      return false;
    }

    VkDeviceSize vertexBufferSize = sizeof(Core::VertexData) * m_Vertices.size();
    void* stagingBufferPointer;
    VkResult result = Core::vkMapMemory(
      m_VulkanRenderer->GetDevice(), m_StagingBuffer.m_Memory, 0, vertexBufferSize, 0, &stagingBufferPointer);

    if (result != VK_SUCCESS) {
      std::cerr << "Could not map host to device memory" << std::endl;
      return false;
    }

    memcpy(stagingBufferPointer, m_Vertices.data(), vertexBufferSize);

    VkMappedMemoryRange memoryRange = {
      VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, // VkStructureType    sType;
      nullptr,                               // const void*        pNext;
      m_StagingBuffer.m_Memory,              // VkDeviceMemory     memory;
      0,                                     // VkDeviceSize       offset;
      VK_WHOLE_SIZE                          // VkDeviceSize       size;
    };

    result = Core::vkFlushMappedMemoryRanges(m_VulkanRenderer->GetDevice(), 1, &memoryRange);
    if (result != VK_SUCCESS) {
      std::cerr << "Could not flush mapped memory" << std::endl;
      return false;
    }

    Core::vkUnmapMemory(m_VulkanRenderer->GetDevice(), m_StagingBuffer.m_Memory);

    m_VertexBuffer.m_Size = vertexBufferSize;
    if (!m_VulkanRenderer->CreateBuffer(m_VertexBuffer,
                                        VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_DST_BIT
                                          | VkBufferUsageFlagBits::VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                        VkMemoryPropertyFlagBits::VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
      std::cerr << "Could not create vertex buffer" << std::endl;
      return false;
    }

    VkCommandBuffer commandBuffer = m_VulkanRenderer->GetTransferCommandBuffer();

    VkCommandBufferBeginInfo beginInfo = {
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, // VkStructureType                          sType;
      nullptr,                                     // const void*                              pNext;
      VkCommandBufferUsageFlagBits::VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, // VkCommandBufferUsageFlags flags;
      nullptr // const VkCommandBufferInheritanceInfo*    pInheritanceInfo;
    };
    Core::vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy copyRegion = {
      0,               // VkDeviceSize    srcOffset;
      0,               // VkDeviceSize    dstOffset;
      vertexBufferSize // VkDeviceSize    size;
    };
    Core::vkCmdCopyBuffer(commandBuffer, m_StagingBuffer.m_Handle, m_VertexBuffer.m_Handle, 1, &copyRegion);
    Core::vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {
      VK_STRUCTURE_TYPE_SUBMIT_INFO, // VkStructureType                sType;
      nullptr,                       // const void*                    pNext;
      0,                             // uint32_t                       waitSemaphoreCount;
      nullptr,                       // const VkSemaphore*             pWaitSemaphores;
      nullptr,                       // const VkPipelineStageFlags*    pWaitDstStageMask;
      1,                             // uint32_t                       commandBufferCount;
      &commandBuffer,                // const VkCommandBuffer*         pCommandBuffers;
      0,                             // uint32_t                       signalSemaphoreCount;
      nullptr                        // const VkSemaphore*             pSignalSemaphores;
    };

    Core::vkQueueSubmit(m_VulkanRenderer->GetTransferQueue(), 1, &submitInfo, nullptr);

    return true;
  }

  void OnWindowClose(Os::Window* window)
  {
    UNREFERENCED_PARAMETER(window);
    m_IsRunning = false;
  }

  [[nodiscard]] bool StartMainLoop()
  {
    auto onWindowCloseWrapper = std::bind(&SampleApp::OnWindowClose, this, std::placeholders::_1);
    m_Window->SetOnWindowClose(onWindowCloseWrapper);

    auto onRenderFrameWrapper =
      std::bind(&SampleApp::RenderFrame, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    m_VulkanRenderer->SetOnRenderFrame(onRenderFrameWrapper);

#ifdef VK_USE_PLATFORM_WIN32_KHR
    HANDLE renderThread = CreateThread(NULL, 0, RenderLoop, reinterpret_cast<void*>(this), 0, NULL);
    m_IsRunning = true;

    while (m_IsRunning) {
      m_Window->PollEvents();
      _mm_pause();
    }

    WaitForSingleObject(renderThread, INFINITE);
    return true;
#endif
  }

  bool RenderFrame(VkCommandBuffer& commandBuffer, VkFramebuffer& framebuffer, Core::ImageData& imageData)
  {
    VkClearValue clearValue = { { (85.0f / 255.0f), (87.0f / 255.0f), (112.0f / 255.0f), 0.0f } };
    VkRenderPassBeginInfo renderPassBeginInfo = {
      VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,                          // VkStructureType        sType;
      nullptr,                                                           // const void*            pNext;
      m_VulkanRenderer->GetRenderPass(),                                 // VkRenderPass           renderPass;
      framebuffer,                                                       // VkFramebuffer          framebuffer;
      { { 0, 0 }, { imageData.m_ImageWidth, imageData.m_ImageHeight } }, // VkRect2D               renderArea;
      1,                                                                 // uint32_t               clearValueCount;
      &clearValue                                                        // const VkClearValue*    pClearValues;
    };

    Core::vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VkSubpassContents::VK_SUBPASS_CONTENTS_INLINE);
    Core::vkCmdBindPipeline(
      commandBuffer, VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS, m_VulkanRenderer->GetPipeline());

    VkViewport viewPort = {
      0.0f,                                        // float    x;
      0.0f,                                        // float    y;
      static_cast<float>(imageData.m_ImageWidth),  // float    width;
      static_cast<float>(imageData.m_ImageHeight), // float    height;
      0.0f,                                        // float    minDepth;
      1.0f                                         // float    maxDepth;
    };

    Core::vkCmdSetViewport(commandBuffer, 0, 1, &viewPort);

    VkRect2D scissor = {
      { 0, 0 },                                           // VkOffset2D    offset;
      { imageData.m_ImageWidth, imageData.m_ImageHeight } // VkExtent2D    extent;
    };
    Core::vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    VkDeviceSize offset = 0;
    Core::vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_VertexBuffer.m_Handle, &offset);
    Core::vkCmdDraw(commandBuffer, static_cast<uint32_t>(m_Vertices.size()), 1, 0, 0);

    Core::vkCmdEndRenderPass(commandBuffer);
    return true;
  }

private:
  static constexpr uint32_t MAX_STAGING_MEMORY_SIZE = 4000;
  std::unique_ptr<Os::Window> m_Window;
  std::unique_ptr<Core::VulkanRenderer> m_VulkanRenderer;
  Core::BufferData m_VertexBuffer;
  Core::BufferData m_StagingBuffer;
  std::vector<Core::VertexData> m_Vertices;

  volatile bool m_IsRunning;
};

int main()
{
  std::filesystem::path const debugFilePath = Os::GetExecutableDirectory() / "debug.txt";
  std::ofstream debugFile(debugFilePath);

  if (!debugFile.is_open()) {
    std::cerr << "Cant create debug file: " << debugFilePath.c_str() << std::endl;
    return 1;
  }

  SampleApp app(debugFile);

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