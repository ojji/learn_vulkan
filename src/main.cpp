#include "core/CopyToLocalBufferJob.h"
#include "core/CopyToLocalImageJob.h"
#include "core/Mat4.h"
#include "core/Transition.h"
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
#include <memory>
#include <mutex>
#include <tchar.h>
#include <thread>
#include <vector>


class SampleApp
{
public:
  Core::Mat4 GetUniformData()
  {
    vk::Extent2D currentExtent = m_VulkanRenderer->GetSwapchainExtent();
    float halfWidth = static_cast<float>(currentExtent.width) / 2.0f;
    float halfHeight = static_cast<float>(currentExtent.height) / 2.0f;
    return Core::Mat4::GetOrthographic(-halfWidth, halfWidth, -halfHeight, halfHeight, -1.0f, 1.0f);
  }

  static DWORD WINAPI RenderThreadStart(LPVOID param)
  {
    SampleApp* app = reinterpret_cast<SampleApp*>(param);
    app->RenderThreadStart();
    return 0;
  }

  static DWORD WINAPI TransferThreadStart(LPVOID param)
  {
    SampleApp* app = reinterpret_cast<SampleApp*>(param);
    app->TransferThreadStart();
    return 0;
  }

  SampleApp(std::ostream& debugStream) :
    m_Window(new Os::Window()),
    m_VulkanRenderer(new Core::VulkanRenderer(debugStream, SampleApp::MAX_FRAMES_IN_FLIGHT)),
    m_Transition(Core::Transition())
  {}

  virtual ~SampleApp() {}

  bool Initialize()
  {
    if (!m_Window->Create(_T("Hello Vulkan!"))) {
      return false;
    }

    if (!m_VulkanRenderer->Initialize(m_Window->GetWindowParameters())) {
      return false;
    }

    std::array<float, 4> color = { (85.0f / 255.0f), (87.0f / 255.0f), (112.0f / 255.0f), 0.0f };
    std::array<float, 4> otherColor = { (179.0f / 255.0f), (147.0f / 255.0f), (29.0f / 255.0f), 0.0f };
    m_Transition = Core::Transition(color, otherColor, 1.0f);

    QueryPerformanceCounter(&m_StartTime);
    QueryPerformanceFrequency(&m_Frequency);

    return true;
  }

  void OnWindowClose(Os::Window* window)
  {
    UNREFERENCED_PARAMETER(window);
    m_IsRunning = false;
  }

  void RenderThreadStart()
  {
    InitializeRenderer();
    while (m_IsRunning) {
      // Draw here if you can
      if (m_VulkanRenderer->CanRender()) {
        Render();
      }
    }

    DestroyRenderer();
  }

  void TransferThreadStart()
  {
    vk::CommandPool graphicsCommandPool = m_VulkanRenderer->CreateGraphicsCommandPool();
    vk::CommandPool transferCommandPool = m_VulkanRenderer->CreateTransferCommandPool();
    vk::CommandBuffer transferCommandBuffer = m_VulkanRenderer->AllocateCommandBuffer(transferCommandPool);
    vk::CommandBuffer graphicsCommandBuffer = m_VulkanRenderer->AllocateCommandBuffer(graphicsCommandPool);

    const vk::DeviceSize nonCoherentAtomSize = m_VulkanRenderer->GetNonCoherentAtomSize();
    assert((nonCoherentAtomSize & (nonCoherentAtomSize - 1)) == 0 && nonCoherentAtomSize != 0);

    Core::BufferData stagingBuffer = m_VulkanRenderer->CreateBuffer(
      SampleApp::STAGING_MEMORY_SIZE,
      { vk::BufferUsageFlagBits::eTransferSrc },
      { vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent });

    void* stagingBufferPtr =
      m_VulkanRenderer->GetDevice().mapMemory(stagingBuffer.m_Memory, 0, stagingBuffer.m_Size, {});

    uintptr_t currentPtr = reinterpret_cast<uintptr_t>(stagingBufferPtr);
    vk::DeviceSize bytesInUse = vk::DeviceSize(0);

    std::shared_ptr<Core::CopyToLocalJob> currentJob;
    while (m_TransferRunning || m_TransferQueue.size() > 0) {
      if (m_TransferQueue.size() > 0) {
        currentJob = nullptr;
        {
          std::lock_guard<std::mutex> lock(m_TransferQueueCriticalSection);
          currentJob = m_TransferQueue.back();
          m_TransferQueue.pop_back();
        }

        if (currentJob) {
          m_VulkanRenderer->GetDevice().resetCommandPool(transferCommandPool, {});
          m_VulkanRenderer->GetDevice().resetCommandPool(graphicsCommandPool, {});
          memcpy(reinterpret_cast<void*>(currentPtr + bytesInUse), currentJob->GetDataPtr(), currentJob->GetSize());

          vk::DeviceSize offsetRoundedDown = bytesInUse - (bytesInUse & (nonCoherentAtomSize - 1));
          vk::DeviceSize sizeRoundedUp =
            ((currentJob->GetSize() + nonCoherentAtomSize - 1) & (~nonCoherentAtomSize + 1));
          if (bytesInUse > offsetRoundedDown) {
            sizeRoundedUp += nonCoherentAtomSize;
          }

          auto mappedMemoryRange = vk::MappedMemoryRange(stagingBuffer.m_Memory, // vk::DeviceMemory memory_ = {},
                                                         offsetRoundedDown,      // vk::DeviceSize offset_ = {},
                                                         sizeRoundedUp           // vk::DeviceSize size_ = {}
          );

          m_VulkanRenderer->GetDevice().flushMappedMemoryRanges(mappedMemoryRange);

          switch (currentJob->GetJobType()) {
          case Core::CopyFlags::ToLocalBuffer: {
            m_VulkanRenderer->CopyToLocalBuffer(std::static_pointer_cast<Core::CopyToLocalBufferJob>(currentJob),
                                                graphicsCommandBuffer,
                                                transferCommandBuffer,
                                                stagingBuffer.m_Handle,
                                                bytesInUse);
          } break;
          case Core::CopyFlags::ToLocalImage: {
            m_VulkanRenderer->CopyToLocalImage(std::static_pointer_cast<Core::CopyToLocalImageJob>(currentJob),
                                               graphicsCommandBuffer,
                                               transferCommandBuffer,
                                               stagingBuffer.m_Handle,
                                               bytesInUse);
          } break;
          default: {
            throw std::runtime_error("Unreachable code reached. Thats a feat!");
          } break;
          }

          bytesInUse += currentJob->GetSize();
        }
      } else {
        _mm_pause();
      }
    }

    m_VulkanRenderer->GetDevice().unmapMemory(stagingBuffer.m_Memory);
    m_VulkanRenderer->FreeBuffer(stagingBuffer);
    m_VulkanRenderer->GetDevice().destroyCommandPool(graphicsCommandPool);
    m_VulkanRenderer->GetDevice().destroyCommandPool(transferCommandPool);
  }

  [[nodiscard]] bool StartMainLoop()
  {
    auto onWindowCloseWrapper = std::bind(&SampleApp::OnWindowClose, this, std::placeholders::_1);
    m_Window->SetOnWindowClose(onWindowCloseWrapper);

#ifdef VK_USE_PLATFORM_WIN32_KHR

    m_IsRunning = true;
    m_TransferRunning = true;
    HANDLE renderThread = CreateThread(NULL, 0, RenderThreadStart, reinterpret_cast<void*>(this), 0, NULL);
    HANDLE transferThread = CreateThread(NULL, 0, TransferThreadStart, reinterpret_cast<void*>(this), 0, NULL);

    while (m_IsRunning) {
      m_Window->PollEvents();
      _mm_pause();
    }

    WaitForSingleObject(renderThread, INFINITE);
    WaitForSingleObject(transferThread, INFINITE);
    return true;
#endif
  }

  void InitializeRenderer()
  {
    m_Vertices = {
      Core::VertexData{ { -256.0f, 256.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },  // bottom left
      Core::VertexData{ { 256.0f, 256.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },   // bottom right
      Core::VertexData{ { -256.0f, -256.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } }, // top left
      Core::VertexData{ { 256.0f, -256.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } }   // top right
    };

    m_VertexBuffer =
      m_VulkanRenderer->CreateBuffer(static_cast<uint32_t>(m_Vertices.size()) * sizeof(Core::VertexData),
                                     { vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer },
                                     { vk::MemoryPropertyFlagBits::eDeviceLocal });


    m_MainCommandPools = std::vector<vk::CommandPool>(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t idx = 0; idx != MAX_FRAMES_IN_FLIGHT; ++idx) {
      m_MainCommandPools[idx] = m_VulkanRenderer->CreateGraphicsCommandPool();
    }

    m_MainCommandBuffers = std::vector<vk::CommandBuffer>(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t idx = 0; idx != MAX_FRAMES_IN_FLIGHT; ++idx) {
      m_MainCommandBuffers[idx] = m_VulkanRenderer->AllocateCommandBuffer(m_MainCommandPools[idx]);
    }

    // create sampler
    auto samplerCreateInfo = vk::SamplerCreateInfo(
      {},                                   // vk::SamplerCreateFlags flags_ = {},
      vk::Filter::eLinear,                  // vk::Filter magFilter_ = vk::Filter::eNearest,
      vk::Filter::eLinear,                  // vk::Filter minFilter_ = vk::Filter::eNearest,
      vk::SamplerMipmapMode::eNearest,      // vk::SamplerMipmapMode mipmapMode_ = vk::SamplerMipmapMode::eNearest,
      vk::SamplerAddressMode::eClampToEdge, // vk::SamplerAddressMode addressModeU_ = vk::SamplerAddressMode::eRepeat,
      vk::SamplerAddressMode::eClampToEdge, // vk::SamplerAddressMode addressModeV_ = vk::SamplerAddressMode::eRepeat,
      vk::SamplerAddressMode::eClampToEdge, // vk::SamplerAddressMode addressModeW_ = vk::SamplerAddressMode::eRepeat,
      0.0f,                                 // float mipLodBias_ = {},
      VK_FALSE,                             // vk::Bool32 anisotropyEnable_ = {},
      1.0f,                                 // float maxAnisotropy_ = {},
      VK_FALSE,                             // vk::Bool32 compareEnable_ = {},
      vk::CompareOp::eAlways,               // vk::CompareOp compareOp_ = vk::CompareOp::eNever,
      0.0f,                                 // float minLod_ = {},
      0.0f,                                 // float maxLod_ = {},
      vk::BorderColor::eFloatTransparentBlack, // vk::BorderColor borderColor_ =
                                               // vk::BorderColor::eFloatTransparentBlack,
      VK_FALSE                                 // vk::Bool32 unnormalizedCoordinates_ = {}
    );
    m_Sampler = m_VulkanRenderer->GetDevice().createSampler(samplerCreateInfo);

    m_VulkanRenderer->InitializeFrameResources();

    auto transferJob = std::shared_ptr<Core::CopyToLocalJob>(
      new Core::CopyToLocalBufferJob(m_VulkanRenderer.get(),
                                     m_Vertices.data(),
                                     static_cast<uint32_t>(m_Vertices.size() * sizeof(Core::VertexData)),
                                     m_VertexBuffer.m_Handle,
                                     vk::DeviceSize(0),
                                     { vk::AccessFlagBits::eVertexAttributeRead },
                                     { vk::PipelineStageFlagBits::eVertexInput },
                                     nullptr));

    AddToTransferQueue(transferJob);
    transferJob->WaitComplete();

    // read texture data
    uint32_t textureWidth, textureHeight;
    std::vector<char> textureData = Os::LoadTextureData("assets/Avatar_cat.png", textureWidth, textureHeight);

    m_Texture =
      m_VulkanRenderer->CreateImage(textureWidth,
                                    textureHeight,
                                    { vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst },
                                    { vk::MemoryPropertyFlagBits::eDeviceLocal });

    auto textureCopyJob =
      std::shared_ptr<Core::CopyToLocalJob>(new Core::CopyToLocalImageJob(m_VulkanRenderer.get(),
                                                                          textureData.data(),
                                                                          textureData.size(),
                                                                          m_Texture.m_Width,
                                                                          m_Texture.m_Height,
                                                                          m_Texture.m_Handle,
                                                                          vk::ImageLayout::eShaderReadOnlyOptimal,
                                                                          vk::AccessFlagBits::eShaderRead,
                                                                          vk::PipelineStageFlagBits::eFragmentShader,
                                                                          nullptr));
    AddToTransferQueue(textureCopyJob);
    textureCopyJob->WaitComplete();

    auto imageInfo = vk::DescriptorImageInfo(
      m_Sampler,                              // vk::Sampler sampler_ = {},
      m_Texture.m_View,                       // vk::ImageView imageView_ = {},
      vk::ImageLayout::eShaderReadOnlyOptimal // vk::ImageLayout imageLayout_ = vk::ImageLayout::eUndefined
    );

    vk::WriteDescriptorSet imageAndSamplerDescriptorWrite =
      vk::WriteDescriptorSet(m_VulkanRenderer->GetDescriptorSet(),      // vk::DescriptorSet dstSet_ = {},
                             0,                                         // uint32_t dstBinding_ = {},
                             0,                                         // uint32_t dstArrayElement_ = {},
                             1,                                         // uint32_t descriptorCount_ = {},
                             vk::DescriptorType::eCombinedImageSampler, // vk::DescriptorType descriptorType_ =
                                                                        // vk::DescriptorType::eSampler,
                             &imageInfo, // const vk::DescriptorImageInfo* pImageInfo_ = {},
                             nullptr,    // const vk::DescriptorBufferInfo* pBufferInfo_ = {},
                             nullptr     // const vk::BufferView* pTexelBufferView_ = {}
      );
    m_VulkanRenderer->GetDevice().updateDescriptorSets(imageAndSamplerDescriptorWrite, nullptr);
  }

  void Render()
  {
    auto [acquireResult, frameResources] = m_VulkanRenderer->AcquireNextFrameResources();
    switch (acquireResult) {
    case vk::Result::eSuccess:
    case vk::Result::eSuboptimalKHR: {
    } break;
    case vk::Result::eErrorOutOfDateKHR: {
#ifdef _DEBUG
      std::cout << "Swapchain image out of date during acquiring, recreating swapchain" << std::endl;
#endif
      m_VulkanRenderer->RecreateSwapchain();
      return;
    } break;
    default:
      throw std::runtime_error("Render error! " + vk::to_string(acquireResult));
    }

    vk::CommandPool currentCommandPool = m_MainCommandPools[frameResources.m_FrameIdx];
    vk::CommandBuffer commandBuffer = m_MainCommandBuffers[frameResources.m_FrameIdx];

    m_VulkanRenderer->GetDevice().resetCommandPool(currentCommandPool, {});

    // @ojji TODO where does this belong?
    Core::Mat4 uniformData = GetUniformData();
    auto uniformTransfer = std::shared_ptr<Core::CopyToLocalBufferJob>(
      new Core::CopyToLocalBufferJob(m_VulkanRenderer.get(),
                                     reinterpret_cast<void*>(uniformData.GetData()),
                                     Core::Mat4::GetSize(),
                                     frameResources.m_UniformBuffer.m_Handle,
                                     vk::DeviceSize(0),
                                     { vk::AccessFlagBits::eShaderRead },
                                     { vk::PipelineStageFlagBits::eVertexShader },
                                     nullptr));

    AddToTransferQueue(uniformTransfer);
    uniformTransfer->WaitComplete();

    auto uniformBufferInfo =
      vk::DescriptorBufferInfo(frameResources.m_UniformBuffer.m_Handle, // vk::Buffer buffer_ = {},
                               vk::DeviceSize(0),                       // vk::DeviceSize offset_ = {},
                               Core::Mat4::GetSize()                          // vk::DeviceSize range_ = {}
      );

    auto uniformBufferDescriptorWrite = vk::WriteDescriptorSet(
      m_VulkanRenderer->GetDescriptorSet(), // vk::DescriptorSet dstSet_ = {},
      1,                                    // uint32_t dstBinding_ = {},
      0,                                    // uint32_t dstArrayElement_ = {},
      1,                                    // uint32_t descriptorCount_ = {},
      vk::DescriptorType::eUniformBuffer,   // vk::DescriptorType descriptorType_ = vk::DescriptorType::eSampler,
      nullptr,                              // const vk::DescriptorImageInfo* pImageInfo_ = {},
      &uniformBufferInfo,                   // const vk::DescriptorBufferInfo* pBufferInfo_ = {},
      nullptr                               // const vk::BufferView* pTexelBufferView_ = {}
    );
    m_VulkanRenderer->GetDevice().updateDescriptorSets(uniformBufferDescriptorWrite, nullptr);

    m_VulkanRenderer->BeginFrame(frameResources, commandBuffer);

    LARGE_INTEGER currentTime, elapsedTimeInMilliSeconds;
    QueryPerformanceCounter(&currentTime);
    elapsedTimeInMilliSeconds.QuadPart = currentTime.QuadPart - m_StartTime.QuadPart;
    elapsedTimeInMilliSeconds.QuadPart *= 1000;
    elapsedTimeInMilliSeconds.QuadPart /= m_Frequency.QuadPart;

    vk::ClearValue clearValue = m_Transition.GetValue(static_cast<float>(elapsedTimeInMilliSeconds.QuadPart));
    auto renderPassBeginInfo = vk::RenderPassBeginInfo(
      m_VulkanRenderer->GetRenderPass(), // vk::RenderPass renderPass_ = {},
      frameResources.m_Framebuffer,      // vk::Framebuffer framebuffer_ = {},
      vk::Rect2D(vk::Offset2D(0, 0),
                 vk::Extent2D(frameResources.m_SwapchainImage.m_ImageWidth,
                              frameResources.m_SwapchainImage.m_ImageHeight)), // vk::Rect2D renderArea_ = {},
      1,                                                                       // uint32_t clearValueCount_ = {},
      &clearValue // const vk::ClearValue* pClearValues_ = {}
    );

    commandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_VulkanRenderer->GetPipeline());
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                     m_VulkanRenderer->GetPipelineLayout(),
                                     0,
                                     m_VulkanRenderer->GetDescriptorSet(),
                                     nullptr);

    auto viewport =
      vk::Viewport(0.0f,                                                              // float x_ = {},
                   0.0f,                                                              // float y_ = {},
                   static_cast<float>(frameResources.m_SwapchainImage.m_ImageWidth),  // float width_ = {},
                   static_cast<float>(frameResources.m_SwapchainImage.m_ImageHeight), // float height_ = {},
                   0.0f,                                                              // float minDepth_ = {},
                   1.0f                                                               // float maxDepth_ = {}
      );

    commandBuffer.setViewport(0, viewport);

    auto scissor = vk::Rect2D(
      vk::Offset2D(0, 0),
      vk::Extent2D(frameResources.m_SwapchainImage.m_ImageWidth, frameResources.m_SwapchainImage.m_ImageHeight));
    commandBuffer.setScissor(0, scissor);
    commandBuffer.bindVertexBuffers(0, m_VertexBuffer.m_Handle, vk::DeviceSize(0));
    commandBuffer.draw(static_cast<uint32_t>(m_Vertices.size()), 1, 0, 0);
    commandBuffer.endRenderPass();

    m_VulkanRenderer->EndFrame(frameResources, commandBuffer);
    vk::Semaphore waitSemaphores[] = { frameResources.m_PresentToDrawSemaphore };
    vk::PipelineStageFlags waitStageMasks[] = { vk::PipelineStageFlagBits::eTransfer };

    auto submitInfo =
      vk::SubmitInfo(1,                                       // uint32_t waitSemaphoreCount_ = {},
                     waitSemaphores,                          // const vk::Semaphore* pWaitSemaphores_ = {},
                     waitStageMasks,                          // const vk::PipelineStageFlags* pWaitDstStageMask_ = {},
                     1,                                       // uint32_t commandBufferCount_ = {},
                     &commandBuffer,                          // const vk::CommandBuffer* pCommandBuffers_ = {},
                     1,                                       // uint32_t signalSemaphoreCount_ = {},
                     &frameResources.m_DrawToPresentSemaphore // const vk::Semaphore* pSignalSemaphores_ = {}
      );
    m_VulkanRenderer->GetDevice().resetFences(frameResources.m_Fence);
    m_VulkanRenderer->SubmitToGraphicsQueue(submitInfo, frameResources.m_Fence);

    vk::Result presentResult = m_VulkanRenderer->PresentFrame(frameResources);
    switch (presentResult) {
    case vk::Result::eSuccess: {
      double frameTimeInMs = m_VulkanRenderer->GetFrameTimeInMs(frameResources.m_FrameStat);
      double fps = 1.0 / (frameTimeInMs / 1'000);
      UNREFERENCED_PARAMETER(fps);
      // std::cout << "GPU time: " << frameTimeInMs << " ms (" << fps << " fps)" << std::endl;
    } break;
    case vk::Result::eErrorOutOfDateKHR:
    case vk::Result::eSuboptimalKHR: {
#ifdef _DEBUG
      std::cout << "Swapchain image suboptimal or out of date during presenting, recreating swapchain" << std::endl;
#endif
      m_VulkanRenderer->RecreateSwapchain();
      return;
    } break;
    default:
      throw std::runtime_error("Render error! " + vk::to_string(presentResult));
    }
  }

  void DestroyRenderer()
  {
    m_TransferRunning = false;
    m_VulkanRenderer->GetDevice().waitIdle();
    m_VulkanRenderer->GetDevice().destroySampler(m_Sampler);
    {
      m_VulkanRenderer->GetDevice().destroyImageView(m_Texture.m_View);
      m_Texture.m_View = nullptr;
      m_VulkanRenderer->GetDevice().freeMemory(m_Texture.m_Memory);
      m_Texture.m_Memory = nullptr;
      m_VulkanRenderer->GetDevice().destroyImage(m_Texture.m_Handle);
      m_Texture.m_Handle = nullptr;
      m_Texture.m_Width = 0;
      m_Texture.m_Height = 0;
    }
    m_VulkanRenderer->FreeBuffer(m_VertexBuffer);
    for (auto& commandPool : m_MainCommandPools) {
      m_VulkanRenderer->GetDevice().destroyCommandPool(commandPool);
    }
  }

private:
  void AddToTransferQueue(std::shared_ptr<Core::CopyToLocalJob> const& job)
  {
    std::lock_guard<std::mutex> lock(m_TransferQueueCriticalSection);
    m_TransferQueue.push_back(job);
  }

  static constexpr uint32_t STAGING_MEMORY_SIZE = 256 * 1024 * 1024;
  static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;
  std::unique_ptr<Os::Window> m_Window;
  std::unique_ptr<Core::VulkanRenderer> m_VulkanRenderer;
  std::vector<std::shared_ptr<Core::CopyToLocalJob>> m_TransferQueue;
  Core::Transition m_Transition;
  LARGE_INTEGER m_StartTime;
  LARGE_INTEGER m_Frequency;

  volatile bool m_IsRunning;
  volatile bool m_TransferRunning;
  std::mutex m_TransferQueueCriticalSection;
  std::vector<vk::CommandPool> m_MainCommandPools;
  std::vector<vk::CommandBuffer> m_MainCommandBuffers;
  std::vector<Core::VertexData> m_Vertices;
  Core::BufferData m_VertexBuffer;
  vk::Sampler m_Sampler;
  Core::ImageData m_Texture;
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