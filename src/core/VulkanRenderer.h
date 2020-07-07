#pragma once

#include <memory>
#include <mutex>
#include <ostream>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "CopyToLocalBufferJob.h"
#include "CopyToLocalImageJob.h"
#include "os/Typedefs.h"
#include "os/Window.h"

namespace Core {

struct FrameStat
{
  uint64_t m_BeginFrameTimestamp;
  uint64_t m_EndFrameTimestamp;
};

struct SwapchainImage
{
  uint32_t m_ImageIdx;
  vk::ImageView m_ImageView;
  uint32_t m_ImageWidth;
  uint32_t m_ImageHeight;
};

struct ImageData
{
  vk::Image m_Handle;
  uint32_t m_Width;
  uint32_t m_Height;
  vk::DeviceMemory m_Memory;
  vk::ImageView m_View;
};

struct BufferData
{
  vk::DeviceSize m_Size;
  vk::DeviceMemory m_Memory;
  vk::Buffer m_Handle;
};

struct FrameResource
{
  uint32_t m_FrameIdx;
  vk::Fence m_Fence;
  vk::Framebuffer m_Framebuffer;
  vk::Semaphore m_PresentToDrawSemaphore;
  vk::Semaphore m_DrawToPresentSemaphore;
  vk::CommandBuffer m_CommandBuffer;
  vk::QueryPool m_QueryPool;
  SwapchainImage m_SwapchainImage;
  FrameStat m_FrameStat;
  BufferData m_UniformBuffer;
};

struct Swapchain
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
  float m_TexCoord[2];
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
  Swapchain m_Swapchain;
  bool m_VsyncEnabled;
  vk::RenderPass m_RenderPass;
  vk::Pipeline m_Pipeline;
  vk::PipelineLayout m_PipelineLayout;
  vk::QueryPool m_QueryPool;
  float m_TimestampPeriod;
  vk::DescriptorSetLayout m_DescriptorSetLayout;
  vk::DescriptorPool m_DescriptorPool;
  vk::DescriptorSet m_DescriptorSet;
  VulkanParameters();
};

class VulkanRenderer
{
public:
  VulkanRenderer(bool vsyncEnabled = false, uint32_t frameResourcesCount = 3);
  VulkanRenderer(VulkanRenderer const& other) = default;
  VulkanRenderer(VulkanRenderer&& other) = default;
  VulkanRenderer& operator=(VulkanRenderer const& other) = default;
  VulkanRenderer& operator=(VulkanRenderer&& other) = default;
  virtual ~VulkanRenderer();

  [[nodiscard]] bool CanRender() const { return m_CanRender; }

  bool Initialize(Os::WindowParameters windowParameters);
  BufferData CreateBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags requiredProperties);
  void FreeBuffer(BufferData& vertexBuffer);
  ImageData CreateImage(uint32_t width,
                        uint32_t height,
                        vk::ImageUsageFlags usage,
                        vk::MemoryPropertyFlags requiredProperties);

  void FreeImage(ImageData& imageData);

  void SubmitToGraphicsQueue(vk::SubmitInfo& submitInfo, vk::Fence fence);
  void SubmitToTransferQueue(vk::SubmitInfo& submitInfo, vk::Fence fence);

  inline vk::RenderPass GetRenderPass() const { return m_VulkanParameters.m_RenderPass; }
  inline vk::Pipeline GetPipeline() const { return m_VulkanParameters.m_Pipeline; }
  inline vk::Device GetDevice() const { return m_VulkanParameters.m_Device; }
  vk::DeviceSize GetNonCoherentAtomSize() const;

  vk::CommandPool CreateGraphicsCommandPool();
  vk::CommandPool CreateTransferCommandPool();
  vk::CommandBuffer VulkanRenderer::AllocateCommandBuffer(vk::CommandPool commandPool);
  void VulkanRenderer::InitializeFrameResources();
  std::tuple<vk::Result, FrameResource> AcquireNextFrameResources();
  void BeginFrame(FrameResource& frameResources, vk::CommandBuffer commandBuffer);
  void EndFrame(FrameResource& frameResources, vk::CommandBuffer commandBuffer);

  vk::Result VulkanRenderer::PresentFrame(FrameResource& frameResources);
  bool RecreateSwapchain();
  double GetFrameTimeInMs(FrameStat const& frameStat);
  void CopyToLocalBuffer(std::shared_ptr<Core::CopyToLocalBufferJob> transferJob,
                         vk::CommandBuffer graphicsCommandBuffer,
                         vk::CommandBuffer transferCommandBuffer,
                         vk::Buffer sourceBuffer,
                         vk::DeviceSize sourceOffset);

  void CopyToLocalImage(std::shared_ptr<Core::CopyToLocalImageJob> transferJob,
                         vk::CommandBuffer graphicsCommandBuffer,
                         vk::CommandBuffer transferCommandBuffer,
                         vk::Buffer sourceBuffer,
                         vk::DeviceSize sourceOffset);

  // @ojji: TODO this does not belong here
  vk::DescriptorSet GetDescriptorSet() { return m_VulkanParameters.m_DescriptorSet; }
  // @ojji: TODO this does not belong here
  vk::PipelineLayout GetPipelineLayout() { return m_VulkanParameters.m_PipelineLayout; }

  [[nodiscard]] vk::Extent2D GetSwapchainExtent() const;

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
  [[nodiscard]] vk::ImageUsageFlags GetSwapchainUsageFlags() const;
  [[nodiscard]] vk::SurfaceTransformFlagBitsKHR GetSwapchainTransform() const;
  [[nodiscard]] vk::PresentModeKHR GetSwapchainPresentMode(
    std::vector<enum vk::PresentModeKHR> const& supportedPresentationModes) const;

  bool CreateGraphicsQueue();
  bool CreateTransferQueue();

  vk::UniqueShaderModule CreateShaderModule(char const* filename);
  vk::PipelineLayout CreatePipelineLayout();

  bool CreateSwapchain();
  bool CreateSwapchainImageViews();

  bool CreateDescriptorSetLayout();
  bool CreateDescriptorPool();
  bool AllocateDescriptorSet();

  bool CreateRenderPass();
  bool CreatePipeline();

  bool CreateSemaphores(FrameResource& frameResource);
  bool CreateFence(FrameResource& frameResource);

  void FreeFrameResource(FrameResource& frameResource);

  bool CreateQueryPool();

  bool CreateFramebuffer(vk::Framebuffer& framebuffer, vk::ImageView& imageView);

  std::vector<FrameResource> m_FrameResources;

protected:
  vk::DynamicLoader m_DynamicLoader;
  VulkanParameters m_VulkanParameters;
  volatile bool m_CanRender;
  volatile bool m_IsRunning;

private:
  uint32_t m_FrameResourcesCount;
  Os::WindowParameters m_WindowParameters;
  volatile uint32_t m_CurrentResourceIdx;
  FrameStat m_FrameStat;
  std::mutex m_GraphicsQueueSubmitCriticalSection;
  std::mutex m_TransferQueueSubmitCriticalSection;
};
} // namespace Core
