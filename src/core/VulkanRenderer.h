#ifndef CORE_VULKANRENDERER_H
#define CORE_VULKANRENDERER_H

#include <ostream>
#include <vector>
#include <vulkan/vulkan.h>

#include "core/VulkanDeleter.h"
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
  VkImageView m_ImageView;
  uint32_t m_ImageWidth;
  uint32_t m_ImageHeight;
};

struct FrameResource
{
  VkFence m_Fence;
  VkFramebuffer m_Framebuffer;
  VkSemaphore m_PresentToDrawSemaphore;
  VkSemaphore m_DrawToPresentSemaphore;
  VkCommandBuffer m_CommandBuffer;
};

struct SwapchainData
{
  VkSwapchainKHR m_Handle;
  VkFormat m_Format;
  std::vector<VkImage> m_Images;
  std::vector<VkImageView> m_ImageViews;
  VkExtent2D m_ImageExtent;
};

struct VertexData
{
  float m_Position[4];
  float m_Color[4];
};

struct BufferData
{
  VkDeviceSize m_Size;
  VkDeviceMemory m_Memory;
  VkBuffer m_Handle;
};

struct VulkanParameters
{
  typedef uint32_t QueueFamilyIdx;
  VkInstance m_Instance;
  VkPhysicalDevice m_PhysicalDevice;
  VkDevice m_Device;
  VkQueue m_GraphicsQueue;
  VkQueue m_TransferQueue;
  QueueFamilyIdx m_GraphicsQueueFamilyIdx;
  QueueFamilyIdx m_TransferQueueFamilyIdx;
  VkSurfaceKHR m_PresentSurface;
  VkSurfaceCapabilitiesKHR m_SurfaceCapabilities;
  SwapchainData m_Swapchain;
  VkCommandPool m_GraphicsCommandPool;
  VkCommandPool m_TransferCommandPool;
  VkCommandBuffer m_TransferCommandBuffer;
  bool m_VsyncEnabled;
  VkRenderPass m_RenderPass;
  VkPipeline m_Pipeline;
  VkQueryPool m_QueryPool;
  float m_TimestampPeriod;
  VulkanParameters();
};

class VulkanRenderer
{
public:
  typedef bool(OnRenderFrameCallback)(VkCommandBuffer& commandBuffer, VkFramebuffer& framebuffer, ImageData& imageData);
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

  bool CreateBuffer(BufferData& buffer, VkBufferUsageFlags usage, VkMemoryPropertyFlags requiredProperties);
  bool CopyBuffer(BufferData& from, BufferData& to, VkDeviceSize size);

  inline VkRenderPass GetRenderPass() const { return m_VulkanParameters.m_RenderPass; }
  inline VkPipeline GetPipeline() const { return m_VulkanParameters.m_Pipeline; }
  inline VkDevice GetDevice() const { return m_VulkanParameters.m_Device; }
  inline VkQueue GetTransferQueue() const { return m_VulkanParameters.m_TransferQueue; };
  inline VkCommandBuffer GetTransferCommandBuffer() const { return m_VulkanParameters.m_TransferCommandBuffer; }

private:
  void Free();
  bool LoadVulkanLibrary();
  [[nodiscard]] bool LoadExportedEntryPoints() const;
  [[nodiscard]] bool LoadGlobalLevelFunctions() const;
  void GetVulkanImplementationVersion(uint32_t* implementationVersion) const;
  [[nodiscard]] bool GetVulkanLayers(std::vector<VkLayerProperties>* layers) const;
  [[nodiscard]] bool GetVulkanInstanceExtensions(std::vector<VkExtensionProperties>* instanceExtensions) const;
  [[nodiscard]] bool RequiredInstanceExtensionsAvailable(std::vector<char const*> const& requiredExtensions) const;
  [[nodiscard]] bool GetVulkanLayerExtensions(char const* layerName,
                                              std::vector<VkExtensionProperties>* layerExtensions) const;
  [[nodiscard]] bool LoadInstanceLevelFunctions() const;
  bool CreateInstance(std::vector<char const*> const& requiredExtensions);
  bool CreatePresentationSurface();
  [[nodiscard]] bool GetVulkanDeviceExtensions(VkPhysicalDevice physicalDevice,
                                               std::vector<VkExtensionProperties>* deviceExtensions) const;
  [[nodiscard]] bool RequiredDeviceExtensionsAvailable(VkPhysicalDevice physicalDevice,
                                                       std::vector<char const*> const& requiredExtensions) const;
  bool GetDeviceSurfaceCapabilities(VkPhysicalDevice physicalDevice,
                                    VkSurfaceKHR surface,
                                    VkSurfaceCapabilitiesKHR* surfaceCapabilities) const;
  bool CreateDevice();
  [[nodiscard]] bool LoadDeviceLevelFunctions() const;
  [[nodiscard]] bool GetSupportedSurfaceFormats(VkPhysicalDevice physicalDevice,
                                                VkSurfaceKHR surface,
                                                std::vector<VkSurfaceFormatKHR>* surfaceFormats) const;
  [[nodiscard]] bool GetSupportedPresentationModes(VkPhysicalDevice physicalDevice,
                                                   VkSurfaceKHR surface,
                                                   std::vector<VkPresentModeKHR>* presentationModes) const;
  [[nodiscard]] uint32_t GetSwapchainImageCount() const;
  [[nodiscard]] VkSurfaceFormatKHR GetSwapchainFormat(
    std::vector<VkSurfaceFormatKHR> const& supportedSurfaceFormats) const;
  [[nodiscard]] VkExtent2D GetSwapchainExtent() const;
  [[nodiscard]] VkImageUsageFlags GetSwapchainUsageFlags() const;
  [[nodiscard]] VkSurfaceTransformFlagBitsKHR GetSwapchainTransform() const;
  [[nodiscard]] VkPresentModeKHR GetSwapchainPresentMode(
    std::vector<enum VkPresentModeKHR> const& supportedPresentationModes) const;

  bool CreateGraphicsQueue();
  bool CreateTransferQueue();

  VulkanDeleter<VkShaderModule, PFN_vkDestroyShaderModule> CreateShaderModule(char const* filename);
  VulkanDeleter<VkPipelineLayout, PFN_vkDestroyPipelineLayout> CreatePipelineLayout();
  static std::vector<char> VulkanRenderer::ReadShaderContent(char const* filename);

  bool AllocateBuffer(BufferData& vertexBuffer, VkMemoryPropertyFlags requiredProperties);
  bool AllocateBufferMemory(VkBuffer buffer, VkDeviceMemory* memory);

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

  bool CreateFramebuffer(VkFramebuffer& framebuffer, VkImageView& imageView);
  bool PrepareAndRecordFrame(VkCommandBuffer commandBuffer, uint32_t acquiredImageIdx, VkFramebuffer& framebuffer);

  std::vector<FrameResource> m_FrameResources;

protected:
  Os::LibraryHandle m_VulkanLoaderHandle;
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