#ifndef CORE_VULKANRENDERER_H
#define CORE_VULKANRENDERER_H

#include <ostream>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "os/Typedefs.h"
#include "os/Window.h"

namespace Core {

struct FrameStat
{
  uint64_t m_BeginFrameTimestamp;
  uint64_t m_EndFrameTimestamp;
};

struct ImageData
{
  vk::ImageView m_ImageView;
  uint32_t m_ImageWidth;
  uint32_t m_ImageHeight;
};

struct FrameResource
{
  vk::Fence m_Fence;
  vk::Framebuffer m_Framebuffer;
  vk::Semaphore m_PresentToDrawSemaphore;
  vk::Semaphore m_DrawToPresentSemaphore;
  vk::CommandBuffer m_CommandBuffer;
};

struct SwapchainData
{
  vk::SwapchainKHR m_Handle;
  vk::Format m_Format;
  std::vector<vk::Image> m_Images;
  std::vector<vk::ImageView> m_ImageViews;
  vk::Extent2D m_ImageExtent;
};

struct VertexData
{
  float m_Position[4];
  float m_Color[4];
};

struct BufferData
{
  vk::DeviceSize m_Size;
  vk::DeviceMemory m_Memory;
  vk::Buffer m_Handle;
};

struct VulkanParameters
{
  typedef uint32_t QueueFamilyIdx;
  vk::Instance m_Instance;
  vk::PhysicalDevice m_PhysicalDevice;
  vk::Device m_Device;
  vk::Queue m_GraphicsQueue;
  vk::Queue m_TransferQueue;
  QueueFamilyIdx m_GraphicsQueueFamilyIdx;
  QueueFamilyIdx m_TransferQueueFamilyIdx;
  vk::SurfaceKHR m_PresentSurface;
  vk::SurfaceCapabilitiesKHR m_SurfaceCapabilities;
  SwapchainData m_Swapchain;
  vk::CommandPool m_GraphicsCommandPool;
  vk::CommandPool m_TransferCommandPool;
  vk::CommandBuffer m_TransferCommandBuffer;
  bool m_VsyncEnabled;
  vk::RenderPass m_RenderPass;
  vk::Pipeline m_Pipeline;
  vk::QueryPool m_QueryPool;
  float m_TimestampPeriod;
  VulkanParameters();
};

class VulkanRenderer
{
public:
  typedef bool(OnRenderFrameCallback)(vk::CommandBuffer& commandBuffer,
                                      vk::Framebuffer& framebuffer,
                                      ImageData& imageData);
  VulkanRenderer(bool vsyncEnabled = false, uint32_t frameResourcesCount = 3);
  VulkanRenderer(std::ostream& debugOutput, bool vsyncEnabled = false, uint32_t frameResourcesCount = 3);
  VulkanRenderer(VulkanRenderer const& other) = default;
  VulkanRenderer(VulkanRenderer&& other) = default;
  VulkanRenderer& operator=(VulkanRenderer const& other) = default;
  VulkanRenderer& operator=(VulkanRenderer&& other) = default;
  virtual ~VulkanRenderer();

  bool Render();
  [[nodiscard]] bool CanRender() const { return m_CanRender; }

  void SetOnRenderFrame(std::function<OnRenderFrameCallback> onRenderFrameCallback);
  bool Initialize(Os::WindowParameters windowParameters);
  void FreeBuffer(BufferData& vertexBuffer);

  bool CreateBuffer(BufferData& buffer, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags requiredProperties);
  bool CopyBuffer(BufferData& from, BufferData& to, VkDeviceSize size);

  inline vk::RenderPass GetRenderPass() const { return m_VulkanParameters.m_RenderPass; }
  inline vk::Pipeline GetPipeline() const { return m_VulkanParameters.m_Pipeline; }
  inline vk::Device GetDevice() const { return m_VulkanParameters.m_Device; }
  inline vk::Queue GetTransferQueue() const { return m_VulkanParameters.m_TransferQueue; };
  inline vk::CommandBuffer GetTransferCommandBuffer() const { return m_VulkanParameters.m_TransferCommandBuffer; }

private:
  void Free();

  uint32_t GetVulkanImplementationVersion() const;
  [[nodiscard]] std::vector<vk::LayerProperties> GetVulkanLayers() const;
  [[nodiscard]] std::vector<vk::ExtensionProperties> GetVulkanInstanceExtensions() const;
  [[nodiscard]] bool RequiredInstanceExtensionsAvailable(std::vector<char const*> const& requiredExtensions) const;
  [[nodiscard]] std::vector<vk::ExtensionProperties> GetVulkanLayerExtensions(std::string const& layerName) const;
  bool CreateInstance(std::vector<char const*> const& requiredExtensions);
  bool CreatePresentationSurface();
  [[nodiscard]] std::vector<vk::ExtensionProperties> GetVulkanDeviceExtensions(vk::PhysicalDevice physicalDevice) const;
  [[nodiscard]] bool RequiredDeviceExtensionsAvailable(vk::PhysicalDevice physicalDevice,
                                                       std::vector<char const*> const& requiredExtensions) const;
  vk::SurfaceCapabilitiesKHR GetDeviceSurfaceCapabilities(vk::PhysicalDevice physicalDevice,
                                                          vk::SurfaceKHR surface) const;
  bool CreateDevice();
  [[nodiscard]] std::vector<vk::SurfaceFormatKHR> GetSupportedSurfaceFormats(vk::PhysicalDevice physicalDevice,
                                                                             vk::SurfaceKHR surface) const;
  [[nodiscard]] std::vector<vk::PresentModeKHR> GetSupportedPresentationModes(vk::PhysicalDevice physicalDevice,
                                                                              vk::SurfaceKHR surface) const;
  [[nodiscard]] uint32_t GetSwapchainImageCount() const;
  [[nodiscard]] vk::SurfaceFormatKHR GetSwapchainFormat(
    std::vector<vk::SurfaceFormatKHR> const& supportedSurfaceFormats) const;
  [[nodiscard]] vk::Extent2D GetSwapchainExtent() const;
  [[nodiscard]] vk::ImageUsageFlags GetSwapchainUsageFlags() const;
  [[nodiscard]] vk::SurfaceTransformFlagBitsKHR GetSwapchainTransform() const;
  [[nodiscard]] vk::PresentModeKHR GetSwapchainPresentMode(
    std::vector<enum vk::PresentModeKHR> const& supportedPresentationModes) const;

  bool CreateGraphicsQueue();
  bool CreateTransferQueue();

  vk::UniqueShaderModule CreateShaderModule(char const* filename);
  vk::UniquePipelineLayout CreatePipelineLayout();
  static std::vector<char> VulkanRenderer::ReadShaderContent(char const* filename);

  bool AllocateBuffer(BufferData& vertexBuffer, vk::MemoryPropertyFlags requiredProperties);
  bool AllocateBufferMemory(vk::Buffer buffer, vk::DeviceMemory* memory);

  bool CreateGraphicsCommandPool();
  bool CreateTransferCommandPool();

  bool CreateSwapchain();
  bool CreateSwapchainImageViews();
  bool RecreateSwapchain();

  bool CreateRenderPass();
  bool CreatePipeline();

  bool AllocateGraphicsCommandBuffer(FrameResource& frameResource);
  bool AllocateTransferCommandBuffer();
  bool CreateSemaphores(FrameResource& frameResource);
  bool CreateFence(FrameResource& frameResource);

  bool CreateFrameResources();
  void FreeFrameResource(FrameResource& frameResource);

  bool CreateQueryPool();

  bool CreateFramebuffer(vk::Framebuffer& framebuffer, vk::ImageView& imageView);
  bool PrepareAndRecordFrame(vk::CommandBuffer commandBuffer, uint32_t acquiredImageIdx, vk::Framebuffer& framebuffer);

  std::vector<FrameResource> m_FrameResources;

protected:
  vk::DynamicLoader m_DynamicLoader;
  VulkanParameters m_VulkanParameters;
  std::ostream& m_DebugOutput;
  volatile bool m_CanRender;
  volatile bool m_IsRunning;

private:
  uint32_t m_FrameResourcesCount;
  Os::WindowParameters m_WindowParameters;
  volatile uint32_t m_CurrentResourceIdx;
  std::function<OnRenderFrameCallback> m_OnRenderFrameCallback;
  FrameStat m_FrameStat;
};
} // namespace Core

#endif // CORE_VULKANRENDERER_H