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
    if (!m_VulkanRenderer->CreateBuffer(
          m_StagingBuffer, { vk::BufferUsageFlagBits::eTransferSrc }, { vk::MemoryPropertyFlagBits::eHostVisible })) {
      std::cerr << "Could not create staging buffer" << std::endl;
      return false;
    }

    VkDeviceSize vertexBufferSize = sizeof(Core::VertexData) * m_Vertices.size();
    void* stagingBufferPointer =
      m_VulkanRenderer->GetDevice().mapMemory(m_StagingBuffer.m_Memory, 0, vertexBufferSize, {});

    memcpy(stagingBufferPointer, m_Vertices.data(), vertexBufferSize);

    auto memoryRange = vk::MappedMemoryRange(m_StagingBuffer.m_Memory, // vk::DeviceMemory memory_ = {},
                                             0,                        // vk::DeviceSize offset_ = {},
                                             VK_WHOLE_SIZE             // vk::DeviceSize size_ = {}
    );

    m_VulkanRenderer->GetDevice().flushMappedMemoryRanges(memoryRange);
    m_VulkanRenderer->GetDevice().unmapMemory(m_StagingBuffer.m_Memory);

    m_VertexBuffer.m_Size = vertexBufferSize;
    if (!m_VulkanRenderer->CreateBuffer(
          m_VertexBuffer,
          { vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer },
          { vk::MemoryPropertyFlagBits::eDeviceLocal })) {
      std::cerr << "Could not create vertex buffer" << std::endl;
      return false;
    }

    auto commandBuffer = m_VulkanRenderer->GetTransferCommandBuffer();

    auto beginInfo = vk::CommandBufferBeginInfo(
      { vk::CommandBufferUsageFlagBits::eOneTimeSubmit }, // vk::CommandBufferUsageFlags flags_ = {},
      nullptr // const vk::CommandBufferInheritanceInfo* pInheritanceInfo_ = {}
    );

    commandBuffer.begin(beginInfo);

    auto copyRegion = vk::BufferCopy(0,               // vk::DeviceSize srcOffset_ = {},
                                     0,               // vk::DeviceSize dstOffset_ = {},
                                     vertexBufferSize // vk::DeviceSize size_ = {}
    );
    commandBuffer.copyBuffer(m_StagingBuffer.m_Handle, m_VertexBuffer.m_Handle, copyRegion);
    commandBuffer.end();

    auto submitInfo = vk::SubmitInfo(0,              // uint32_t waitSemaphoreCount_ = {},
                                     nullptr,        // const vk::Semaphore* pWaitSemaphores_ = {},
                                     nullptr,        // const vk::PipelineStageFlags* pWaitDstStageMask_ = {},
                                     1,              // uint32_t commandBufferCount_ = {},
                                     &commandBuffer, // const vk::CommandBuffer* pCommandBuffers_ = {},
                                     0,              // uint32_t signalSemaphoreCount_ = {},
                                     nullptr         // const vk::Semaphore* pSignalSemaphores_ = {}
    );

    m_VulkanRenderer->GetTransferQueue().submit(submitInfo, nullptr);
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

  bool RenderFrame(vk::CommandBuffer& commandBuffer, vk::Framebuffer& framebuffer, Core::ImageData& imageData)
  {
    vk::ClearValue clearValue = std::array<float, 4>({ (85.0f / 255.0f), (87.0f / 255.0f), (112.0f / 255.0f), 0.0f });
    auto renderPassBeginInfo = vk::RenderPassBeginInfo(
      m_VulkanRenderer->GetRenderPass(), // vk::RenderPass renderPass_ = {},
      framebuffer,                       // vk::Framebuffer framebuffer_ = {},
      vk::Rect2D(vk::Offset2D(0, 0),
                 vk::Extent2D(imageData.m_ImageWidth, imageData.m_ImageHeight)), // vk::Rect2D renderArea_ = {},
      1,                                                                         // uint32_t clearValueCount_ = {},
      &clearValue // const vk::ClearValue* pClearValues_ = {}
    );

    commandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_VulkanRenderer->GetPipeline());

    auto viewport = vk::Viewport(0.0f,                                        // float x_ = {},
                                 0.0f,                                        // float y_ = {},
                                 static_cast<float>(imageData.m_ImageWidth),  // float width_ = {},
                                 static_cast<float>(imageData.m_ImageHeight), // float height_ = {},
                                 0.0f,                                        // float minDepth_ = {},
                                 1.0f                                         // float maxDepth_ = {}
    );

    commandBuffer.setViewport(0, viewport);

    auto scissor = vk::Rect2D(vk::Offset2D(0, 0), vk::Extent2D(imageData.m_ImageWidth, imageData.m_ImageHeight));
    commandBuffer.setScissor(0, scissor);
    commandBuffer.bindVertexBuffers(0, m_VertexBuffer.m_Handle, vk::DeviceSize(0));
    commandBuffer.draw(static_cast<uint32_t>(m_Vertices.size()), 1, 0, 0);
    commandBuffer.endRenderPass();
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