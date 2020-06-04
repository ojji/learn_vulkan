#include "core/CopyToLocalBufferJob.h"
#include "core/CopyToLocalImageJob.h"
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
  static DWORD WINAPI RenderLoop(LPVOID param)
  {
    SampleApp* app = reinterpret_cast<SampleApp*>(param);

    std::vector<Core::VertexData> vertices = {
      Core::VertexData{ { -0.7f, 0.7f, 0.0f, 1.0f }, { 0.0f, 1.0f } },  // bottom left
      Core::VertexData{ { 0.7f, 0.7f, 0.0f, 1.0f }, { 1.0f, 1.0f } },   // bottom right
      Core::VertexData{ { -0.7f, -0.7f, 0.0f, 1.0f }, { 0.0f, 0.0f } }, // top left
      Core::VertexData{ { 0.7f, -0.7f, 0.0f, 1.0f }, { 1.0f, 0.0f } }   // top right
    };

    vk::DeviceSize verticesSize = static_cast<uint32_t>(vertices.size()) * sizeof(Core::VertexData);
    Core::BufferData vertexBuffer = app->m_VulkanRenderer->CreateBuffer(
      verticesSize,
      { vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer },
      { vk::MemoryPropertyFlagBits::eDeviceLocal });

    std::vector<vk::CommandPool> commandPools = std::vector<vk::CommandPool>(app->MAX_FRAMES_IN_FLIGHT);
    for (uint32_t idx = 0; idx != app->MAX_FRAMES_IN_FLIGHT; ++idx) {
      commandPools[idx] = app->m_VulkanRenderer->CreateGraphicsCommandPool();
    }

    std::vector<vk::CommandBuffer> commandBuffers = std::vector<vk::CommandBuffer>(app->MAX_FRAMES_IN_FLIGHT);
    for (uint32_t idx = 0; idx != app->MAX_FRAMES_IN_FLIGHT; ++idx) {
      commandBuffers[idx] = app->m_VulkanRenderer->AllocateCommandBuffer(commandPools[idx]);
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
    vk::Sampler sampler = app->m_VulkanRenderer->GetDevice().createSampler(samplerCreateInfo);

    app->m_VulkanRenderer->InitializeFrameResources();
    bool firstFrame = true;

    while (app->m_IsRunning) {
      // Draw here if you can
      if (app->m_VulkanRenderer->CanRender()) {
        auto [acquireResult, frameResources] = app->m_VulkanRenderer->AcquireNextFrameResources();

        switch (acquireResult) {
        case vk::Result::eSuccess:
        case vk::Result::eSuboptimalKHR: {
        } break;
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

        if (firstFrame) {
          {
            auto transferJob = std::shared_ptr<Core::CopyToLocalJob>(
              new Core::CopyToLocalBufferJob(app->m_VulkanRenderer.get(),
                                             vertices.data(),
                                             verticesSize,
                                             vertexBuffer.m_Handle,
                                             vk::DeviceSize(0),
                                             { vk::AccessFlagBits::eVertexAttributeRead },
                                             { vk::PipelineStageFlagBits::eVertexInput }));

            {
              std::lock_guard<std::mutex> lock(app->m_TransferQueueCriticalSection);
              app->m_TransferQueue.push_back(transferJob);
            }

            transferJob->WaitComplete();

            // read texture data
            uint32_t textureWidth, textureHeight;
            std::vector<char> textureData = Os::LoadTextureData("assets/Avatar_cat.png", textureWidth, textureHeight);

            Core::ImageData texture = app->m_VulkanRenderer->CreateImage(
              textureWidth,
              textureHeight,
              { vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst },
              { vk::MemoryPropertyFlagBits::eDeviceLocal });

            auto textureCopyJob = std::shared_ptr<Core::CopyToLocalJob>(
              new Core::CopyToLocalImageJob(app->m_VulkanRenderer.get(),
                                            textureData.data(),
                                            textureData.size(),
                                            texture.m_Width,
                                            texture.m_Height,
                                            texture.m_Handle,
                                            vk::ImageLayout::eShaderReadOnlyOptimal,
                                            vk::AccessFlagBits::eShaderRead,
                                            vk::PipelineStageFlagBits::eFragmentShader));
            {
              std::lock_guard<std::mutex> lock(app->m_TransferQueueCriticalSection);
              app->m_TransferQueue.push_back(textureCopyJob);
            }
            textureCopyJob->WaitComplete();

            auto imageInfo = vk::DescriptorImageInfo(
              sampler,                                // vk::Sampler sampler_ = {},
              texture.m_View,                         // vk::ImageView imageView_ = {},
              vk::ImageLayout::eShaderReadOnlyOptimal // vk::ImageLayout imageLayout_ = vk::ImageLayout::eUndefined
            );

            vk::WriteDescriptorSet descriptorWrite =
              vk::WriteDescriptorSet(app->m_VulkanRenderer->GetDescriptorSet(), // vk::DescriptorSet dstSet_ = {},
                                     0,                                         // uint32_t dstBinding_ = {},
                                     0,                                         // uint32_t dstArrayElement_ = {},
                                     1,                                         // uint32_t descriptorCount_ = {},
                                     vk::DescriptorType::eCombinedImageSampler, // vk::DescriptorType descriptorType_ =
                                                                                // vk::DescriptorType::eSampler,
                                     &imageInfo, // const vk::DescriptorImageInfo* pImageInfo_ = {},
                                     nullptr,    // const vk::DescriptorBufferInfo* pBufferInfo_ = {},
                                     nullptr     // const vk::BufferView* pTexelBufferView_ = {}
              );
            app->m_VulkanRenderer->GetDevice().updateDescriptorSets(descriptorWrite, nullptr);
            firstFrame = false;
          }
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
                     vk::Extent2D(frameResources.m_SwapchainImage.m_ImageWidth,
                                  frameResources.m_SwapchainImage.m_ImageHeight)), // vk::Rect2D renderArea_ = {},
          1,                                                                       // uint32_t clearValueCount_ = {},
          &clearValue // const vk::ClearValue* pClearValues_ = {}
        );

        commandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, app->m_VulkanRenderer->GetPipeline());
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                         app->m_VulkanRenderer->GetPipelineLayout(),
                                         0,
                                         app->m_VulkanRenderer->GetDescriptorSet(),
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
        commandBuffer.bindVertexBuffers(0, vertexBuffer.m_Handle, vk::DeviceSize(0));
        commandBuffer.draw(static_cast<uint32_t>(vertices.size()), 1, 0, 0);
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
          UNREFERENCED_PARAMETER(fps);
          // std::cout << "GPU time: " << frameTimeInMs << " ms (" << fps << " fps)" << std::endl;
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
    app->m_VulkanRenderer->GetDevice().destroySampler(sampler);
    app->m_VulkanRenderer->FreeBuffer(vertexBuffer);
    for (auto& commandPool : commandPools) {
      app->m_VulkanRenderer->GetDevice().destroyCommandPool(commandPool);
    }
    return 0;
  }

  static DWORD WINAPI TransferLoop(LPVOID param)
  {
    SampleApp* app = reinterpret_cast<SampleApp*>(param);

    vk::CommandPool graphicsCommandPool = app->m_VulkanRenderer->CreateGraphicsCommandPool();
    vk::CommandPool transferCommandPool = app->m_VulkanRenderer->CreateTransferCommandPool();
    vk::CommandBuffer transferCommandBuffer = app->m_VulkanRenderer->AllocateCommandBuffer(transferCommandPool);
    vk::CommandBuffer graphicsCommandBuffer = app->m_VulkanRenderer->AllocateCommandBuffer(graphicsCommandPool);

    const vk::DeviceSize nonCoherentAtomSize = app->m_VulkanRenderer->GetNonCoherentAtomSize();
    assert((nonCoherentAtomSize & (nonCoherentAtomSize - 1)) == 0 && nonCoherentAtomSize != 0);


    Core::BufferData stagingBuffer = app->m_VulkanRenderer->CreateBuffer(
      SampleApp::STAGING_MEMORY_SIZE,
      { vk::BufferUsageFlagBits::eTransferSrc },
      { vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent });

    void* stagingBufferPtr =
      app->m_VulkanRenderer->GetDevice().mapMemory(stagingBuffer.m_Memory, 0, stagingBuffer.m_Size, {});

    uintptr_t currentPtr = reinterpret_cast<uintptr_t>(stagingBufferPtr);
    vk::DeviceSize bytesInUse = vk::DeviceSize(0);

    std::shared_ptr<Core::CopyToLocalJob> currentJob;
    while (app->m_IsRunning) {
      if (app->m_TransferQueue.size() > 0) {
        currentJob = nullptr;
        {
          std::lock_guard<std::mutex> lock(app->m_TransferQueueCriticalSection);
          currentJob = app->m_TransferQueue.back();
          app->m_TransferQueue.pop_back();
        }

        if (currentJob) {
          app->m_VulkanRenderer->GetDevice().resetCommandPool(transferCommandPool, {});
          app->m_VulkanRenderer->GetDevice().resetCommandPool(graphicsCommandPool, {});
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

          app->m_VulkanRenderer->GetDevice().flushMappedMemoryRanges(mappedMemoryRange);

          switch (currentJob->GetJobType()) {
          case Core::CopyFlags::ToLocalBuffer: {
            app->m_VulkanRenderer->CopyToLocalBuffer(std::static_pointer_cast<Core::CopyToLocalBufferJob>(currentJob),
                                                     graphicsCommandBuffer,
                                                     transferCommandBuffer,
                                                     stagingBuffer.m_Handle,
                                                     bytesInUse);
          } break;
          case Core::CopyFlags::ToLocalImage: {
            app->m_VulkanRenderer->CopyToLocalImage(std::static_pointer_cast<Core::CopyToLocalImageJob>(currentJob),
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

    app->m_VulkanRenderer->GetDevice().unmapMemory(stagingBuffer.m_Memory);
    app->m_VulkanRenderer->FreeBuffer(stagingBuffer);
    app->m_VulkanRenderer->GetDevice().destroyCommandPool(graphicsCommandPool);
    app->m_VulkanRenderer->GetDevice().destroyCommandPool(transferCommandPool);
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
  static constexpr uint32_t STAGING_MEMORY_SIZE = 256 * 1024 * 1024;
  static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;
  std::unique_ptr<Os::Window> m_Window;
  std::unique_ptr<Core::VulkanRenderer> m_VulkanRenderer;
  std::vector<std::shared_ptr<Core::CopyToLocalJob>> m_TransferQueue;
  Core::Transition m_Transition;
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