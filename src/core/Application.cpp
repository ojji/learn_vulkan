#include "Application.h"
#include "utils/Logger.h"

namespace Core {
Application::Application() :
  m_Window(new Os::Window()),
  m_VulkanRenderer(new Core::VulkanRenderer(true, MAX_FRAMES_IN_FLIGHT))
{}

Application::~Application()
{}

bool Application::Start()
{
  auto onWindowCloseWrapper = std::bind(&Application::OnWindowClose, this, std::placeholders::_1);
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
#endif
  return true;
}

bool Application::Initialize(wchar_t const title[], uint32_t width, uint32_t height)
{
  if (!m_Window->Create(title, width, height)) {
    return false;
  }
  if (!m_VulkanRenderer->Initialize(m_Window->GetWindowParameters())) {
    return false;
  }

  return true;
}

void Application::AddToTransferQueue(std::shared_ptr<Core::CopyToLocalJob> const& job)
{
  std::lock_guard<std::mutex> lock(m_TransferQueueCriticalSection);
  m_TransferQueue.push_back(job);
}


void Application::RenderThreadStart()
{
  InitializeRendererCore();
  while (m_IsRunning) {
    // Draw here if you can
    if (m_VulkanRenderer->CanRender()) {
      RenderCore();
    }
  }

  DestroyRendererCore();
}

void Application::TransferThreadStart()
{
  vk::CommandPool graphicsCommandPool = m_VulkanRenderer->CreateGraphicsCommandPool();
  vk::CommandPool transferCommandPool = m_VulkanRenderer->CreateTransferCommandPool();
  vk::CommandBuffer transferCommandBuffer = m_VulkanRenderer->AllocateCommandBuffer(transferCommandPool);
  vk::CommandBuffer graphicsCommandBuffer = m_VulkanRenderer->AllocateCommandBuffer(graphicsCommandPool);

  const vk::DeviceSize nonCoherentAtomSize = m_VulkanRenderer->GetNonCoherentAtomSize();
  assert((nonCoherentAtomSize & (nonCoherentAtomSize - 1)) == 0 && nonCoherentAtomSize != 0);

  Core::BufferData stagingBuffer = m_VulkanRenderer->CreateBuffer(
    STAGING_MEMORY_SIZE,
    { vk::BufferUsageFlagBits::eTransferSrc },
    { vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent });

  void* stagingBufferPtr = m_VulkanRenderer->GetDevice().mapMemory(stagingBuffer.m_Memory, 0, stagingBuffer.m_Size, {});

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
        vk::DeviceSize sizeRoundedUp = ((currentJob->GetSize() + nonCoherentAtomSize - 1) & (~nonCoherentAtomSize + 1));
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

void Application::InitializeRendererCore()
{
  m_MainCommandPools = std::vector<vk::CommandPool>(MAX_FRAMES_IN_FLIGHT);
  for (uint32_t idx = 0; idx != MAX_FRAMES_IN_FLIGHT; ++idx) {
    m_MainCommandPools[idx] = m_VulkanRenderer->CreateGraphicsCommandPool();
  }

  m_MainCommandBuffers = std::vector<vk::CommandBuffer>(MAX_FRAMES_IN_FLIGHT);
  for (uint32_t idx = 0; idx != MAX_FRAMES_IN_FLIGHT; ++idx) {
    m_MainCommandBuffers[idx] = m_VulkanRenderer->AllocateCommandBuffer(m_MainCommandPools[idx]);
  }

  m_VulkanRenderer->InitializeFrameResources();

  InitializeRenderer();
}

void Application::RenderCore()
{
  auto [acquireResult, frameResources] = m_VulkanRenderer->AcquireNextFrameResources();
  switch (acquireResult) {
  case vk::Result::eSuccess:
  case vk::Result::eSuboptimalKHR: {
  } break;
  case vk::Result::eErrorOutOfDateKHR: {
    Utils::Logger::Get().LogDebugEx(
      "Swapchain image out of date during acquiring, recreating swapchain", "Renderer", __FILE__, __func__, __LINE__);
    m_VulkanRenderer->RecreateSwapchain();
    return;
  } break;
  default:
    throw std::runtime_error("Render error! " + vk::to_string(acquireResult));
  }

  vk::CommandPool currentCommandPool = m_MainCommandPools[frameResources.m_FrameIdx];
  vk::CommandBuffer commandBuffer = m_MainCommandBuffers[frameResources.m_FrameIdx];

  m_VulkanRenderer->GetDevice().resetCommandPool(currentCommandPool, {});
  PreRender(frameResources);

  m_VulkanRenderer->BeginFrame(frameResources, commandBuffer);

  Render(frameResources, commandBuffer);

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
    PostRender(frameResources.m_FrameStat);
  } break;
  case vk::Result::eErrorOutOfDateKHR:
  case vk::Result::eSuboptimalKHR: {
    Utils::Logger::Get().LogDebugEx("Swapchain image suboptimal or out of date during presenting, recreating swapchain",
                                    "Renderer",
                                    __FILE__,
                                    __func__,
                                    __LINE__);
    m_VulkanRenderer->RecreateSwapchain();
    return;
  } break;
  default:
    throw std::runtime_error("Render error! " + vk::to_string(presentResult));
  }
}

void Application::DestroyRendererCore()
{
  m_TransferRunning = false;
  m_VulkanRenderer->GetDevice().waitIdle();
  for (auto& commandPool : m_MainCommandPools) {
    m_VulkanRenderer->GetDevice().destroyCommandPool(commandPool);
  }
  OnDestroyRenderer();
}

void Application::OnWindowClose(Os::Window* window)
{
  UNREFERENCED_PARAMETER(window);
  m_IsRunning = false;
  OnWindowClosed();
}

DWORD WINAPI Application::RenderThreadStart(LPVOID param)
{
  Application* app = reinterpret_cast<Application*>(param);
  app->RenderThreadStart();
  return 0;
}

DWORD WINAPI Application::TransferThreadStart(LPVOID param)
{
  Application* app = reinterpret_cast<Application*>(param);
  app->TransferThreadStart();
  return 0;
}
} // namespace Core
