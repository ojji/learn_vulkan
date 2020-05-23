#include "core/VulkanFunctions.h"
#include "core/VulkanRenderer.h"
#include "os/Common.h"
#include "os/Window.h"
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <tchar.h>
#include <thread>
#include <vector>

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

class CopyToLocalBufferJob
{
public:
  CopyToLocalBufferJob(void* data,
                       vk::DeviceSize size,
                       vk::Buffer destinationBuffer,
                       vk::DeviceSize destinationOffset) :
    m_Data(data),
    m_Size(size),
    m_DestinationBuffer(destinationBuffer),
    m_DestinationOffset(destinationOffset){};

private:
  void* m_Data;
  vk::DeviceSize m_Size;
  vk::Buffer m_DestinationBuffer;
  vk::DeviceSize m_DestinationOffset;
};

class SampleApp
{
public:
  static DWORD WINAPI RenderLoop(LPVOID param)
  {
    SampleApp* app = reinterpret_cast<SampleApp*>(param);
    std::vector<vk::CommandPool> commandPools = std::vector<vk::CommandPool>(app->MAX_FRAMES_IN_FLIGHT);
    for (uint32_t idx = 0; idx != app->MAX_FRAMES_IN_FLIGHT; ++idx) {
      commandPools[idx] = app->m_VulkanRenderer->CreateGraphicsCommandPool();
    }

    std::vector<vk::CommandBuffer> commandBuffers = std::vector<vk::CommandBuffer>(app->MAX_FRAMES_IN_FLIGHT);
    for (uint32_t idx = 0; idx != app->MAX_FRAMES_IN_FLIGHT; ++idx) {
      commandBuffers[idx] = app->m_VulkanRenderer->AllocateCommandBuffer(commandPools[idx]);
    }

    app->m_VulkanRenderer->InitializeFrameResources();

    while (app->m_IsRunning) {
      // Draw here if you can
      if (app->m_VulkanRenderer->CanRender()) {
        auto [acquireResult, frameResources] = app->m_VulkanRenderer->AcquireNextFrameResources();

        switch (acquireResult) {
        case vk::Result::eSuccess: {
        } break;
        case vk::Result::eSuboptimalKHR:
        case vk::Result::eErrorOutOfDateKHR: {
#ifdef _DEBUG
          std::cout << "Swapchain image suboptimal or out of date during acquiring, recreating swapchain" << std::endl;
#endif
          app->m_VulkanRenderer->RecreateSwapchain();
          continue;
        } break;
        default:
          throw std::runtime_error("Render error! " + vk::to_string(acquireResult));
        }

        vk::CommandPool currentCommandPool = commandPools[frameResources.m_FrameIdx];
        vk::CommandBuffer commandBuffer = commandBuffers[frameResources.m_FrameIdx];

        app->m_VulkanRenderer->GetDevice().resetCommandPool(currentCommandPool, {});
        app->m_VulkanRenderer->BeginFrame(frameResources, commandBuffer);

        LARGE_INTEGER currentTime, elapsedTimeInMilliSeconds;
        QueryPerformanceCounter(&currentTime);
        elapsedTimeInMilliSeconds.QuadPart = currentTime.QuadPart - app->m_StartTime.QuadPart;
        elapsedTimeInMilliSeconds.QuadPart *= 1000;
        elapsedTimeInMilliSeconds.QuadPart /= app->m_Frequency.QuadPart;

        vk::ClearValue clearValue = app->m_Transition.GetValue(static_cast<float>(elapsedTimeInMilliSeconds.QuadPart));
        auto renderPassBeginInfo = vk::RenderPassBeginInfo(
          app->m_VulkanRenderer->GetRenderPass(), // vk::RenderPass renderPass_ = {},
          frameResources.m_Framebuffer,           // vk::Framebuffer framebuffer_ = {},
          vk::Rect2D(vk::Offset2D(0, 0),
                     vk::Extent2D(frameResources.m_ImageData.m_ImageWidth,
                                  frameResources.m_ImageData.m_ImageHeight)), // vk::Rect2D renderArea_ = {},
          1,                                                                  // uint32_t clearValueCount_ = {},
          &clearValue // const vk::ClearValue* pClearValues_ = {}
        );

        commandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, app->m_VulkanRenderer->GetPipeline());

        auto viewport =
          vk::Viewport(0.0f,                                                         // float x_ = {},
                       0.0f,                                                         // float y_ = {},
                       static_cast<float>(frameResources.m_ImageData.m_ImageWidth),  // float width_ = {},
                       static_cast<float>(frameResources.m_ImageData.m_ImageHeight), // float height_ = {},
                       0.0f,                                                         // float minDepth_ = {},
                       1.0f                                                          // float maxDepth_ = {}
          );

        commandBuffer.setViewport(0, viewport);

        auto scissor =
          vk::Rect2D(vk::Offset2D(0, 0),
                     vk::Extent2D(frameResources.m_ImageData.m_ImageWidth, frameResources.m_ImageData.m_ImageHeight));
        commandBuffer.setScissor(0, scissor);
        commandBuffer.bindVertexBuffers(0, app->m_VertexBuffer.m_Handle, vk::DeviceSize(0));
        commandBuffer.draw(static_cast<uint32_t>(app->m_Vertices.size()), 1, 0, 0);
        commandBuffer.endRenderPass();

        app->m_VulkanRenderer->EndFrame(frameResources, commandBuffer);

        vk::PipelineStageFlags waitStageMask = { vk::PipelineStageFlagBits::eTransfer };
        auto submitInfo =
          vk::SubmitInfo(1,                                        // uint32_t waitSemaphoreCount_ = {},
                         &frameResources.m_PresentToDrawSemaphore, // const vk::Semaphore* pWaitSemaphores_ = {},
                         &waitStageMask, // const vk::PipelineStageFlags* pWaitDstStageMask_ = {},
                         1,              // uint32_t commandBufferCount_ = {},
                         &commandBuffer, // const vk::CommandBuffer* pCommandBuffers_ = {},
                         1,              // uint32_t signalSemaphoreCount_ = {},
                         &frameResources.m_DrawToPresentSemaphore // const vk::Semaphore* pSignalSemaphores_ = {}
          );

        app->m_VulkanRenderer->GetDevice().resetFences(frameResources.m_Fence);
        app->m_VulkanRenderer->SubmitToGraphicsQueue(submitInfo, frameResources.m_Fence);

        vk::Result presentResult = app->m_VulkanRenderer->PresentFrame(frameResources);
        switch (presentResult) {
        case vk::Result::eSuccess: {
          double frameTimeInMs = app->m_VulkanRenderer->GetFrameTimeInMs(frameResources.m_FrameStat);
          double fps = 1.0 / (frameTimeInMs / 1'000);
          std::cout << "GPU time: " << frameTimeInMs << " ms (" << fps << " fps)" << std::endl;
        } break;
        case vk::Result::eErrorOutOfDateKHR:
        case vk::Result::eSuboptimalKHR: {
#ifdef _DEBUG
          std::cout << "Swapchain image suboptimal or out of date during presenting, recreating swapchain" << std::endl;
#endif
          app->m_VulkanRenderer->RecreateSwapchain();
          continue;
        } break;
        default:
          throw std::runtime_error("Render error! " + vk::to_string(presentResult));
        }
      }
    }

    app->m_VulkanRenderer->GetDevice().waitIdle();
    for (auto& commandPool : commandPools) {
      app->m_VulkanRenderer->GetDevice().destroyCommandPool(commandPool);
    }
    return 0;
  }

  static DWORD WINAPI TransferLoop(LPVOID param)
  {
    SampleApp* app = reinterpret_cast<SampleApp*>(param);

    while (app->m_IsRunning) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      _mm_pause();
    }
    return 0;
  }

  SampleApp(std::ostream& debugStream) :
    m_Window(new Os::Window()),
    m_VulkanRenderer(new Core::VulkanRenderer(debugStream, SampleApp::MAX_FRAMES_IN_FLIGHT)),
    m_Transition(Transition())
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

    std::array<float, 4> color = { (85.0f / 255.0f), (87.0f / 255.0f), (112.0f / 255.0f), 0.0f };
    std::array<float, 4> otherColor = { (179.0f / 255.0f), (147.0f / 255.0f), (29.0f / 255.0f), 0.0f };
    m_Transition = Transition(color, otherColor, 1.0f);

    QueryPerformanceCounter(&m_StartTime);
    QueryPerformanceFrequency(&m_Frequency);

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

    auto commandPool = m_VulkanRenderer->CreateTransferCommandPool();
    auto allocateInfo = vk::CommandBufferAllocateInfo(
      commandPool,                          // vk::CommandPool commandPool_ = {},
      { vk::CommandBufferLevel::ePrimary }, // vk::CommandBufferLevel level_ = vk::CommandBufferLevel::ePrimary,
      1                                     // uint32_t commandBufferCount_ = {}
    );

    vk::CommandBuffer commandBuffer = m_VulkanRenderer->GetDevice().allocateCommandBuffers(allocateInfo)[0];

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

    m_VulkanRenderer->SubmitToTransferQueue(submitInfo, nullptr);
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

#ifdef VK_USE_PLATFORM_WIN32_KHR
    HANDLE renderThread = CreateThread(NULL, 0, RenderLoop, reinterpret_cast<void*>(this), 0, NULL);
    HANDLE transferThread = CreateThread(NULL, 0, TransferLoop, reinterpret_cast<void*>(this), 0, NULL);
    m_IsRunning = true;

    while (m_IsRunning) {
      m_Window->PollEvents();
      _mm_pause();
    }

    WaitForSingleObject(renderThread, INFINITE);
    WaitForSingleObject(transferThread, INFINITE);
    return true;
#endif
  }

private:
  static constexpr uint32_t MAX_STAGING_MEMORY_SIZE = 4000;
  static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;
  std::unique_ptr<Os::Window> m_Window;
  std::unique_ptr<Core::VulkanRenderer> m_VulkanRenderer;
  Core::BufferData m_VertexBuffer;
  Core::BufferData m_StagingBuffer;
  std::vector<Core::VertexData> m_Vertices;
  std::vector<CopyToLocalBufferJob> m_TransferQueue;
  Transition m_Transition;
  LARGE_INTEGER m_StartTime;
  LARGE_INTEGER m_Frequency;

  volatile bool m_IsRunning;
  std::mutex m_TransferQueueCriticalSection;
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