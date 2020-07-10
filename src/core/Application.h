#pragma once

#include "core/VulkanRenderer.h"
#include "os/Window.h"
#include <memory>
#include <mutex>
#include <vector>

namespace Core {
class Application
{
public:
  Application();
  virtual ~Application();

  bool Start();

protected:
  inline Core::VulkanRenderer* Renderer() const { return m_VulkanRenderer.get(); }
  inline Os::Window* GetWindow() const { return m_Window.get(); }

  virtual void InitializeRenderer() = 0;
  virtual void PreRender(Core::FrameResource const& frameResources) = 0;
  virtual void Render(Core::FrameResource const& frameResources, vk::CommandBuffer const& commandBuffer) = 0;
  virtual void PostRender(Core::FrameStat const& frameStats) = 0;
  virtual void OnDestroyRenderer() = 0;
  virtual void OnWindowClosed(){};

  bool Initialize(wchar_t const title[], uint32_t width, uint32_t height);
  void AddToTransferQueue(std::shared_ptr<Core::CopyToLocalJob> const& job);

private:
  void RenderThreadStart();
  void TransferThreadStart();
  void InitializeRendererCore();
  void RenderCore();
  void DestroyRendererCore();
  void OnWindowClose(Os::Window* window);

  static DWORD WINAPI RenderThreadStart(LPVOID param);
  static DWORD WINAPI TransferThreadStart(LPVOID param);

  static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;
  static constexpr uint32_t STAGING_MEMORY_SIZE = 256 * 1024 * 1024;
  std::unique_ptr<Os::Window> m_Window;
  std::unique_ptr<Core::VulkanRenderer> m_VulkanRenderer;
  volatile bool m_IsRunning;
  volatile bool m_TransferRunning;
  std::mutex m_TransferQueueCriticalSection;
  std::vector<std::shared_ptr<Core::CopyToLocalJob>> m_TransferQueue;

  std::vector<vk::CommandPool> m_MainCommandPools;
  std::vector<vk::CommandBuffer> m_MainCommandBuffers;
};
} // namespace Core
