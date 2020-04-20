#ifndef CORE_VULKANAPP_H
#define CORE_VULKANAPP_H

#include <ostream>
#include <vector>
#include <vulkan/vulkan.h>

#include "core/VulkanDeleter.h"
#include "os/TypeDefs.h"

namespace Core {
struct FrameResource {
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

struct VertexBuffer
{
  VkDeviceMemory m_Memory;
  VkBuffer m_Handle;
};

struct VulkanParameters
{
  typedef uint32_t QueueFamilyIdx;
  VkInstance m_Instance;
  VkPhysicalDevice m_PhysicalDevice;
  QueueFamilyIdx m_PresentQueueFamilyIdx;
  VkDevice m_Device;
  VkQueue m_Queue;
  VkSurfaceKHR m_PresentSurface;
  VkSurfaceCapabilitiesKHR m_SurfaceCapabilities;
  SwapchainData m_Swapchain;
  VkCommandPool m_PresentCommandPool;
  bool m_VsyncEnabled;
  VkRenderPass m_RenderPass;
  VkPipeline m_Pipeline;
  VulkanParameters();
};

class VulkanApp
{
public:
  VulkanApp(bool vsyncEnabled = false);
  VulkanApp(std::ostream& debugOutput, bool vsyncEnabled = false);
  VulkanApp(VulkanApp const& other) = default;
  VulkanApp(VulkanApp&& other) = default;
  VulkanApp& operator=(VulkanApp const& other) = default;
  VulkanApp& operator=(VulkanApp&& other) = default;
  virtual ~VulkanApp();

  [[nodiscard]] bool CanRender() const { return m_CanRender; }
  virtual bool Render() = 0;

  bool PrepareVulkan(Os::WindowParameters windowParameters);

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

  bool CreateQueue();

  VulkanDeleter<VkShaderModule, PFN_vkDestroyShaderModule> CreateShaderModule(char const* filename);
  VulkanDeleter<VkPipelineLayout, PFN_vkDestroyPipelineLayout> CreatePipelineLayout();
  static std::vector<char> VulkanApp::ReadShaderContent(char const* filename);
  bool AllocateCommandBuffer(FrameResource& frameResource);
  bool AllocateVertexBuffer(VertexBuffer& vertexBuffer);
  bool AllocateBufferMemory(VkBuffer buffer, VkDeviceMemory* memory);

  bool CreateSemaphores(FrameResource &frameResource);
  bool CreateFence(FrameResource &frameResource);

protected:
  bool CreateCommandPool();
  bool CreateSwapchain();
  bool CreateSwapchainImageViews();
  bool CreateRenderPass();
  bool CreatePipeline();

  bool CreateVertexBuffer(std::vector<VertexData> const& vertexData, VertexBuffer& buffer);
  void FreeVertexBuffer(VertexBuffer& vertexBuffer);
  bool CreateFrameResources(uint32_t resourceCount, std::vector<FrameResource> &resources);
  void FreeFrameResource(FrameResource& frameResource);
  bool RecreateSwapchain();

  Os::LibraryHandle m_VulkanLoaderHandle;
  VulkanParameters m_VulkanParameters;
  std::ostream& m_DebugOutput;
  volatile bool m_CanRender;

private:
  Os::WindowParameters m_WindowParameters;
};
} // namespace Core

#endif // CORE_VULKANAPP_H