#include "VulkanRenderer.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "VulkanFunctions.h"
#include "os/Common.h"
#include "os/Window.h"

namespace Core {
VulkanParameters::VulkanParameters() :
  m_Instance(nullptr),
  m_PhysicalDevice(nullptr),
  m_Device(nullptr),
  m_GraphicsQueue(nullptr),
  m_TransferQueue(nullptr),
  m_GraphicsQueueFamilyIdx(std::numeric_limits<QueueFamilyIdx>::max()),
  m_TransferQueueFamilyIdx(std::numeric_limits<QueueFamilyIdx>::max()),
  m_PresentSurface(nullptr),
  m_SurfaceCapabilities(),
  m_Swapchain(SwapchainData()),
  m_GraphicsCommandPool(nullptr),
  m_TransferCommandPool(nullptr),
  m_TransferCommandBuffer(nullptr),
  m_VsyncEnabled(false),
  m_RenderPass(nullptr),
  m_Pipeline(nullptr)
{}

VulkanRenderer::VulkanRenderer(bool vsyncEnabled, uint32_t frameResourcesCount) :
  VulkanRenderer(std::cout, vsyncEnabled, frameResourcesCount)
{}

VulkanRenderer::VulkanRenderer(std::ostream& debugOutput, bool vsyncEnabled, uint32_t frameResourcesCount) :
  m_VulkanLoaderHandle(nullptr),
  m_VulkanParameters(VulkanParameters()),
  m_DebugOutput(debugOutput),
  m_WindowParameters(Os::WindowParameters()),
  m_FrameResourcesCount(frameResourcesCount),
  m_FrameStat(FrameStat())
{
  m_DebugOutput << std::showbase;
  m_VulkanParameters.m_VsyncEnabled = vsyncEnabled;
}

void VulkanRenderer::Free()
{
  if (m_VulkanParameters.m_Device) {
    vkDeviceWaitIdle(m_VulkanParameters.m_Device);

    if (m_VulkanParameters.m_QueryPool) {
      vkDestroyQueryPool(m_VulkanParameters.m_Device, m_VulkanParameters.m_QueryPool, nullptr);
    }

    for (uint32_t i = 0; i != m_FrameResources.size(); ++i) {
      FreeFrameResource(m_FrameResources[i]);
    }

    if (m_VulkanParameters.m_Pipeline) {
      vkDestroyPipeline(m_VulkanParameters.m_Device, m_VulkanParameters.m_Pipeline, nullptr);
    }

    if (m_VulkanParameters.m_RenderPass) {
      vkDestroyRenderPass(m_VulkanParameters.m_Device, m_VulkanParameters.m_RenderPass, nullptr);
      m_VulkanParameters.m_RenderPass = nullptr;
    }

    for (auto& imageView : m_VulkanParameters.m_Swapchain.m_ImageViews) {
      vkDestroyImageView(m_VulkanParameters.m_Device, imageView, nullptr);
    }

    if (m_VulkanParameters.m_Swapchain.m_Handle) {
      vkDestroySwapchainKHR(m_VulkanParameters.m_Device, m_VulkanParameters.m_Swapchain.m_Handle, nullptr);
    }

    if (m_VulkanParameters.m_GraphicsCommandPool) {
      vkDestroyCommandPool(m_VulkanParameters.m_Device, m_VulkanParameters.m_GraphicsCommandPool, nullptr);
      m_VulkanParameters.m_GraphicsCommandPool = nullptr;
    }

    if (m_VulkanParameters.m_TransferCommandBuffer) {
      vkFreeCommandBuffers(m_VulkanParameters.m_Device,
                           m_VulkanParameters.m_TransferCommandPool,
                           1,
                           &m_VulkanParameters.m_TransferCommandBuffer);
      m_VulkanParameters.m_TransferCommandBuffer = nullptr;
    }

    if (m_VulkanParameters.m_TransferCommandPool) {
      vkDestroyCommandPool(m_VulkanParameters.m_Device, m_VulkanParameters.m_TransferCommandPool, nullptr);
      m_VulkanParameters.m_TransferCommandPool = nullptr;
    }

    vkDestroyDevice(m_VulkanParameters.m_Device, nullptr);
    m_VulkanParameters.m_Device = nullptr;
    m_VulkanParameters.m_GraphicsQueue = nullptr;
    m_VulkanParameters.m_TransferQueue = nullptr;
  }

  if (m_VulkanParameters.m_PresentSurface) {
    vkDestroySurfaceKHR(m_VulkanParameters.m_Instance, m_VulkanParameters.m_PresentSurface, nullptr);
    m_VulkanParameters.m_PresentSurface = nullptr;
  }

  if (m_VulkanParameters.m_Instance) {
    vkDestroyInstance(m_VulkanParameters.m_Instance, nullptr);
    m_VulkanParameters.m_Instance = nullptr;
    m_VulkanParameters.m_PhysicalDevice = nullptr;
  }

  if (m_VulkanLoaderHandle) {
#ifdef VK_USE_PLATFORM_WIN32_KHR
    if (!FreeLibrary(m_VulkanLoaderHandle)) {
      std::cerr << "Could not free Vulkan library, error: " << GetLastError() << std::endl;
    }
#endif
    m_VulkanLoaderHandle = nullptr;
  }
}

VulkanRenderer::~VulkanRenderer()
{
  Free();
}

void VulkanRenderer::SetOnRenderFrame(std::function<OnRenderFrameCallback> onRenderFrameCallback)
{
  m_OnRenderFrameCallback = onRenderFrameCallback;
}

bool VulkanRenderer::Initialize(Os::WindowParameters windowParameters)
{
  m_WindowParameters = windowParameters;
  if (!LoadVulkanLibrary()) {
    std::cerr << "Could not load Vulkan library" << std::endl;
    return false;
  }
  if (!LoadExportedEntryPoints()) {
    std::cerr << "Could not load exported entry points" << std::endl;
    return false;
  }
  if (!LoadGlobalLevelFunctions()) {
    std::cerr << "Could not load global level entry points" << std::endl;
    return false;
  }

  uint32_t implementationVersion;
  GetVulkanImplementationVersion(&implementationVersion);

#ifdef _DEBUG
  m_DebugOutput << "Vulkan implementation version: " << VK_VERSION_MAJOR(implementationVersion) << "."
                << VK_VERSION_MINOR(implementationVersion) << "." << VK_VERSION_PATCH(implementationVersion)
                << std::endl;
#endif

  std::vector<VkLayerProperties> layers{};
  if (!GetVulkanLayers(&layers)) {
    std::cerr << "Could not enumerate the available layers" << std::endl;
    return false;
  }

#ifdef _DEBUG
  m_DebugOutput << "\nAvailable layers:" << std::endl;
  for (decltype(layers)::size_type idx = 0; idx != layers.size(); ++idx) {
    if (idx != 0) {
      m_DebugOutput << std::endl;
    }
    m_DebugOutput << "\t#" << idx << " layerName: " << layers[idx].layerName << std::endl
                  << "\t#" << idx << " specVersion: " << VK_EXPAND_VERSION(layers[idx].specVersion) << std::endl
                  << "\t#" << idx << " implementationVersion: " << layers[idx].implementationVersion << std::endl
                  << "\t#" << idx << " description: " << layers[idx].description << std::endl;
  }
#endif
  std::vector<VkExtensionProperties> instanceExtensions{};
  if (!GetVulkanInstanceExtensions(&instanceExtensions)) {
    std::cerr << "Could not enumerate the available instance extensions" << std::endl;
    return false;
  }

#ifdef _DEBUG
  m_DebugOutput << "\nAvailable instance extensions:" << std::endl;
  for (decltype(instanceExtensions)::size_type idx = 0; idx != instanceExtensions.size(); ++idx) {
    m_DebugOutput << "\t#" << idx << " extensionName: " << instanceExtensions[idx].extensionName
                  << " (specVersion: " << instanceExtensions[idx].specVersion << ")" << std::endl;
  }
#endif

  std::vector<char const*> const requiredExtensions = { VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef VK_USE_PLATFORM_WIN32_KHR
                                                        VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#endif
  };

  if (!RequiredInstanceExtensionsAvailable(requiredExtensions)) {
    std::cerr << "Required instance extensions are not available" << std::endl;
    return false;
  }

  for (decltype(layers)::size_type layerIdx = 0; layerIdx != layers.size(); ++layerIdx) {
    const char* layerName = layers[layerIdx].layerName;

    std::vector<VkExtensionProperties> layerExtensions{};
    if (!GetVulkanLayerExtensions(layerName, &layerExtensions)) {
      std::cerr << "Could not enumerate the available " << layerName << " extensions" << std::endl;
      return false;
    }

#ifdef _DEBUG
    m_DebugOutput << "\nAvailable " << layerName << " extensions:" << std::endl;
    for (decltype(layerExtensions)::size_type idx = 0; idx != layerExtensions.size(); ++idx) {
      m_DebugOutput << "\t#" << idx << " extensionName: " << layerExtensions[idx].extensionName
                    << " (specVersion: " << layerExtensions[idx].specVersion << ")" << std::endl;
    }
#endif
  }

  if (!CreateInstance(requiredExtensions)) {
    return false;
  }

  if (!LoadInstanceLevelFunctions()) {
    std::cerr << "Could not load instance level entry points" << std::endl;
    return false;
  }

  if (!CreatePresentationSurface()) {
    std::cerr << "Could not create the presentation surface" << std::endl;
    return false;
  }

  if (!CreateDevice()) {
    return false;
  }

  if (!LoadDeviceLevelFunctions()) {
    return false;
  }

  if (!CreateGraphicsQueue()) {
    return false;
  }

  if (!CreateTransferQueue()) {
    return false;
  }

  if (!CreateGraphicsCommandPool()) {
    return false;
  }

  if (!CreateTransferCommandPool()) {
    return false;
  }

  if (!AllocateTransferCommandBuffer()) {
    return false;
  }

  if (!CreateSwapchain()) {
    return false;
  }

  if (!CreateRenderPass()) {
    return false;
  }

  if (!CreatePipeline()) {
    return false;
  }

  if (!CreateFrameResources()) {
    return false;
  }

  if (!CreateQueryPool()) {
    return false;
  }

  return true;
}

bool VulkanRenderer::LoadVulkanLibrary()
{
#ifdef VK_USE_PLATFORM_WIN32_KHR
  m_VulkanLoaderHandle = LoadLibrary(L"vulkan-1.dll");
#endif
  return m_VulkanLoaderHandle != nullptr;
}

bool VulkanRenderer::LoadExportedEntryPoints() const
{
#ifdef VK_USE_PLATFORM_WIN32_KHR
#  define LoadProcAddress GetProcAddress
#endif

#define VK_EXPORTED_FUNCTION(fn)                                                                                       \
  fn = reinterpret_cast<PFN_##fn>(LoadProcAddress(m_VulkanLoaderHandle, #fn));                                         \
  if (!(fn)) {                                                                                                         \
    std::cerr << "Could not load exported function: " << #fn << std::endl;                                             \
    return false;                                                                                                      \
  }

#include "VulkanFunctions.inl"
  return true;
}

bool VulkanRenderer::LoadGlobalLevelFunctions() const
{
#define VK_GLOBAL_FUNCTION(fn)                                                                                         \
  fn = reinterpret_cast<PFN_##fn>(vkGetInstanceProcAddr(nullptr, #fn));                                                \
  if (!(fn)) {                                                                                                         \
    std::cerr << "Could not load global level function: " << #fn << std::endl;                                         \
    return false;                                                                                                      \
  }

#include "VulkanFunctions.inl"
  return true;
}

void VulkanRenderer::GetVulkanImplementationVersion(uint32_t* implementationVersion) const
{
  vkEnumerateInstanceVersion(implementationVersion);
}

bool VulkanRenderer::GetVulkanLayers(std::vector<VkLayerProperties>* layers) const
{
  uint32_t instanceLayerPropertiesCount;
  VkResult result = vkEnumerateInstanceLayerProperties(&instanceLayerPropertiesCount, nullptr);
  if (result != VK_SUCCESS) {
    return false;
  }

  layers->clear();
  layers->resize(instanceLayerPropertiesCount);

  result = vkEnumerateInstanceLayerProperties(&instanceLayerPropertiesCount, layers->data());
  if (result != VK_SUCCESS) {
    return false;
  }

  return true;
}

bool VulkanRenderer::GetVulkanInstanceExtensions(std::vector<VkExtensionProperties>* instanceExtensions) const
{
  uint32_t instanceExtensionsCount;
  VkResult result = vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionsCount, nullptr);
  if (result != VK_SUCCESS) {
    return false;
  }

  instanceExtensions->clear();
  instanceExtensions->resize(instanceExtensionsCount);

  result = vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionsCount, instanceExtensions->data());
  if (result != VK_SUCCESS) {
    return false;
  }

  return true;
}

bool VulkanRenderer::RequiredInstanceExtensionsAvailable(std::vector<char const*> const& requiredExtensions) const
{
  std::vector<VkExtensionProperties> instanceExtensions{};
  if (!GetVulkanInstanceExtensions(&instanceExtensions)) {
    return false;
  }

  return std::all_of(requiredExtensions.cbegin(), requiredExtensions.cend(), [&](char const* extensionName) {
    for (auto const& instanceExtension : instanceExtensions) {
      if (strcmp(instanceExtension.extensionName, extensionName) == 0) {
        return true;
      }
    }
    return false;
  });
}

bool VulkanRenderer::GetVulkanLayerExtensions(char const* layerName,
                                              std::vector<VkExtensionProperties>* layerExtensions) const
{
  uint32_t layerExtensionsCount;
  VkResult result = vkEnumerateInstanceExtensionProperties(layerName, &layerExtensionsCount, nullptr);
  if (result != VK_SUCCESS) {
    return false;
  }

  layerExtensions->clear();
  layerExtensions->resize(layerExtensionsCount);

  result = vkEnumerateInstanceExtensionProperties(layerName, &layerExtensionsCount, layerExtensions->data());
  if (result != VK_SUCCESS) {
    return false;
  }

  return true;
}

bool VulkanRenderer::LoadInstanceLevelFunctions() const
{
#define VK_INSTANCE_FUNCTION(fn)                                                                                       \
  fn = reinterpret_cast<PFN_##fn>(vkGetInstanceProcAddr(m_VulkanParameters.m_Instance, #fn));                          \
  if (!(fn)) {                                                                                                         \
    std::cerr << "Could not load instance level function: " << #fn << std::endl;                                       \
    return false;                                                                                                      \
  }

#include "VulkanFunctions.inl"
  return true;
}

bool VulkanRenderer::CreateInstance(std::vector<char const*> const& requiredExtensions)
{
  VkApplicationInfo applicationInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                        nullptr,
                                        "Learn Vulkan",
                                        VK_MAKE_VERSION(1, 0, 0),
                                        "Learn Vulkan Engine",
                                        VK_MAKE_VERSION(1, 0, 0),
                                        VK_API_VERSION_1_2 };

  std::vector<char const*> requestedLayers = {
#ifdef _DEBUG
    "VK_LAYER_KHRONOS_validation"
#endif
  };

  VkInstanceCreateInfo createInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                      nullptr,
                                      0,
                                      &applicationInfo,
                                      static_cast<uint32_t>(requestedLayers.size()),
                                      requestedLayers.data(),
                                      static_cast<uint32_t>(requiredExtensions.size()),
                                      requiredExtensions.data() };

  VkResult const result = vkCreateInstance(&createInfo, nullptr, &m_VulkanParameters.m_Instance);
  if (result != VK_SUCCESS) {
    std::cerr << "Could not create Vulkan instance: " << result << std::endl;
    return false;
  }

  return true;
}

bool VulkanRenderer::CreatePresentationSurface()
{
#ifdef VK_USE_PLATFORM_WIN32_KHR
  VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
                                                    nullptr,
                                                    0,
                                                    m_WindowParameters.m_Instance,
                                                    m_WindowParameters.m_Handle };

  VkResult const result = vkCreateWin32SurfaceKHR(
    m_VulkanParameters.m_Instance, &surfaceCreateInfo, nullptr, &m_VulkanParameters.m_PresentSurface);

  if (result != VK_SUCCESS) {
    return false;
  }

  return true;
#endif
}

bool VulkanRenderer::GetVulkanDeviceExtensions(VkPhysicalDevice physicalDevice,
                                               std::vector<VkExtensionProperties>* deviceExtensions) const
{
  uint32_t deviceExtensionCount;
  VkResult result = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &deviceExtensionCount, nullptr);
  if (result != VK_SUCCESS) {
    return false;
  }

  deviceExtensions->clear();
  deviceExtensions->resize(deviceExtensionCount);

  result =
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &deviceExtensionCount, deviceExtensions->data());
  if (result != VK_SUCCESS) {
    return false;
  }

  return true;
}

bool VulkanRenderer::RequiredDeviceExtensionsAvailable(VkPhysicalDevice physicalDevice,
                                                       std::vector<char const*> const& requiredExtensions) const
{
  std::vector<VkExtensionProperties> availableExtensions{};
  if (!GetVulkanDeviceExtensions(physicalDevice, &availableExtensions)) {
    return false;
  }

  return std::all_of(requiredExtensions.cbegin(), requiredExtensions.cend(), [&](char const* extensionName) {
    for (auto const& deviceExtension : availableExtensions) {
      if (strcmp(extensionName, deviceExtension.extensionName) == 0) {
        return true;
      }
    }
    return false;
  });
}

bool VulkanRenderer::GetDeviceSurfaceCapabilities(VkPhysicalDevice physicalDevice,
                                                  VkSurfaceKHR surface,
                                                  VkSurfaceCapabilitiesKHR* surfaceCapabilities) const
{
  // Get surface capabilities
  VkResult const result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, surfaceCapabilities);

  if (result != VK_SUCCESS) {
    std::cerr << "Could not query the devices surface capabilities: " << result << std::endl;
    return false;
  }

#ifdef _DEBUG
  m_DebugOutput << "\nSurface capabilities:" << std::endl
                << "\tminImageCount: " << m_VulkanParameters.m_SurfaceCapabilities.minImageCount << std::endl
                << "\tmaxImageCount: " << m_VulkanParameters.m_SurfaceCapabilities.maxImageCount << std::endl
                << "\tcurrentExtent: " << VK_EXPAND_EXTENT2D(m_VulkanParameters.m_SurfaceCapabilities.currentExtent)
                << std::endl
                << "\tminImageExtent: " << VK_EXPAND_EXTENT2D(m_VulkanParameters.m_SurfaceCapabilities.minImageExtent)
                << std::endl
                << "\tmaxImageExtent: " << VK_EXPAND_EXTENT2D(m_VulkanParameters.m_SurfaceCapabilities.maxImageExtent)
                << std::endl
                << "\tmaxImageArrayLayers: " << m_VulkanParameters.m_SurfaceCapabilities.maxImageArrayLayers
                << std::endl
                << "\tsupportedTransforms: " << std::hex << m_VulkanParameters.m_SurfaceCapabilities.supportedTransforms
                << std::dec << std::endl
                << "\tcurrentTransform: " << std::hex << m_VulkanParameters.m_SurfaceCapabilities.currentTransform
                << std::dec << std::endl
                << "\tsupportedCompositeAlpha: " << std::hex
                << m_VulkanParameters.m_SurfaceCapabilities.supportedCompositeAlpha << std::dec << std::endl
                << "\tsupportedUsageFlags: " << std::hex << m_VulkanParameters.m_SurfaceCapabilities.supportedUsageFlags
                << std::dec << std::endl;
#endif

  return true;
}

bool VulkanRenderer::CreateDevice()
{
  uint32_t numberOfDevices;
  VkResult result;
  std::vector<char const*> const requiredDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

  result = vkEnumeratePhysicalDevices(m_VulkanParameters.m_Instance, &numberOfDevices, nullptr);
  if (result != VK_SUCCESS || numberOfDevices == 0) {
    return false;
  }
  std::vector<VkPhysicalDevice> physicalDevices(numberOfDevices, VK_NULL_HANDLE);
  result = vkEnumeratePhysicalDevices(m_VulkanParameters.m_Instance, &numberOfDevices, physicalDevices.data());
  if (result != VK_SUCCESS) {
    return false;
  }

  std::vector<VkPhysicalDeviceProperties2> deviceProperties(physicalDevices.size());
  std::vector<VkPhysicalDeviceVulkan11Properties> vulkan11Properties(physicalDevices.size());
  std::vector<VkPhysicalDeviceVulkan12Properties> vulkan12Properties(physicalDevices.size());
  std::vector<VkPhysicalDeviceFeatures2> deviceFeatures(physicalDevices.size());

  bool presentQueueFound = false;
  bool transferQueueFound = false;

  for (decltype(physicalDevices)::size_type deviceIdx = 0; deviceIdx != physicalDevices.size(); ++deviceIdx) {
    deviceProperties[deviceIdx].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    vkGetPhysicalDeviceProperties2(physicalDevices[deviceIdx], &deviceProperties[deviceIdx]);
    // Additional properties to query
    vulkan12Properties[deviceIdx].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
    vulkan12Properties[deviceIdx].pNext = nullptr;

    vulkan11Properties[deviceIdx].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
    vulkan11Properties[deviceIdx].pNext = &vulkan12Properties[deviceIdx];

    deviceProperties[deviceIdx].pNext = &vulkan11Properties[deviceIdx];
    vkGetPhysicalDeviceProperties2(physicalDevices[deviceIdx], &deviceProperties[deviceIdx]);

    m_VulkanParameters.m_TimestampPeriod = deviceProperties[deviceIdx].properties.limits.timestampPeriod;

#ifdef _DEBUG
    m_DebugOutput << "Device #" << deviceIdx << ": " << std::endl
                  << "\tName: " << deviceProperties[deviceIdx].properties.deviceName
                  << " (type: " << deviceProperties[deviceIdx].properties.deviceType << ")" << std::endl
                  << "\tApi version: " << VK_EXPAND_VERSION(deviceProperties[deviceIdx].properties.apiVersion)
                  << std::endl
                  << "\tDriver version: " << VK_EXPAND_VERSION(deviceProperties[deviceIdx].properties.driverVersion)
                  << std::endl
                  << "\tSome limits: " << std::endl
                  << "\t\tmaxImageDimension2D: " << deviceProperties[deviceIdx].properties.limits.maxImageDimension2D
                  << std::endl
                  << "\t\tframebufferColorSampleCounts: " << std::hex
                  << deviceProperties[deviceIdx].properties.limits.framebufferColorSampleCounts << std::dec << std::endl
                  << "\t\tframebufferDepthSampleCounts: " << std::hex
                  << deviceProperties[deviceIdx].properties.limits.framebufferDepthSampleCounts << std::dec << std::endl
                  << "\t\ttimestampPeriod: " << deviceProperties[deviceIdx].properties.limits.timestampPeriod
                  << std::endl;
#endif

    // Get device features
    deviceFeatures[deviceIdx].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    vkGetPhysicalDeviceFeatures2(physicalDevices[deviceIdx], &deviceFeatures[deviceIdx]);

#ifdef _DEBUG
    m_DebugOutput << "\nA few device features: " << std::endl
                  << "\tgeometryShader: " << deviceFeatures[deviceIdx].features.geometryShader << std::endl
                  << "\ttessellationShader: " << deviceFeatures[deviceIdx].features.tessellationShader << std::endl
                  << "\tsamplerAnisotropy: " << deviceFeatures[deviceIdx].features.samplerAnisotropy << std::endl
                  << "\tfragmentStoresAndAtomics: " << deviceFeatures[deviceIdx].features.fragmentStoresAndAtomics
                  << std::endl
                  << "\talphaToOne: " << deviceFeatures[deviceIdx].features.alphaToOne << std::endl;

#endif

    std::vector<VkExtensionProperties> deviceExtensions{};
    if (!GetVulkanDeviceExtensions(physicalDevices[deviceIdx], &deviceExtensions)) {
      std::cerr << "Could not enumerate device extensions: " << result << std::endl;
      return false;
    }

#ifdef _DEBUG
    m_DebugOutput << "\nDevice extensions: " << std::endl;
    for (decltype(deviceExtensions)::size_type deviceExtensionIdx = 0; deviceExtensionIdx != deviceExtensions.size();
         ++deviceExtensionIdx) {
      m_DebugOutput << "\t#" << deviceExtensionIdx
                    << " extensionName: " << deviceExtensions[deviceExtensionIdx].extensionName
                    << " (specVersion: " << deviceExtensions[deviceExtensionIdx].specVersion << ")" << std::endl;
    }
#endif

    // Get device queue family properties
    uint32_t queueFamilyCount;
    vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevices[deviceIdx], &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties2> queueFamilyProperties(queueFamilyCount);
    for (decltype(queueFamilyProperties)::size_type queueFamilyIdx = 0; queueFamilyIdx != queueFamilyCount;
         ++queueFamilyIdx) {
      queueFamilyProperties[queueFamilyIdx].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
      queueFamilyProperties[queueFamilyIdx].pNext = nullptr;
    }
    vkGetPhysicalDeviceQueueFamilyProperties2(
      physicalDevices[deviceIdx], &queueFamilyCount, queueFamilyProperties.data());

#ifdef _DEBUG
    m_DebugOutput << "\nQueue family count: " << queueFamilyCount << std::endl;
    for (decltype(queueFamilyProperties)::size_type queueFamilyIdx = 0; queueFamilyIdx != queueFamilyCount;
         ++queueFamilyIdx) {
      if (queueFamilyIdx != 0) {
        m_DebugOutput << std::endl;
      }
      m_DebugOutput << "\t#" << std::dec << queueFamilyIdx << " queueFlags: " << std::hex
                    << queueFamilyProperties[queueFamilyIdx].queueFamilyProperties.queueFlags << std::endl
                    << "\t#" << std::dec << queueFamilyIdx
                    << " queueCount: " << queueFamilyProperties[queueFamilyIdx].queueFamilyProperties.queueCount
                    << std::endl
                    << "\t#" << std::dec << queueFamilyIdx << " timestampValidBits: "
                    << queueFamilyProperties[queueFamilyIdx].queueFamilyProperties.timestampValidBits << std::endl
                    << "\t#" << std::dec << queueFamilyIdx << " minImageTransferGranularity: "
                    << VK_EXPAND_EXTENT3D(
                         queueFamilyProperties[queueFamilyIdx].queueFamilyProperties.minImageTransferGranularity)
                    << std::endl;
    }
#endif

    if (RequiredDeviceExtensionsAvailable(physicalDevices[deviceIdx], requiredDeviceExtensions)) {
      for (decltype(queueFamilyProperties)::size_type queueFamilyIdx = 0;
           queueFamilyIdx != queueFamilyCount && !m_VulkanParameters.m_PhysicalDevice;
           ++queueFamilyIdx) {
        VkBool32 isSurfacePresentationSupported = VK_FALSE;
        result = vkGetPhysicalDeviceSurfaceSupportKHR(
          physicalDevices[deviceIdx], 0, m_VulkanParameters.m_PresentSurface, &isSurfacePresentationSupported);

        if (result != VK_SUCCESS) {
          std::cerr << "Error querying WSI surface support: " << result << std::endl;
          return false;
        }

        if (queueFamilyProperties[queueFamilyIdx].queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT
            && !presentQueueFound && isSurfacePresentationSupported == VK_TRUE) {
          m_VulkanParameters.m_PhysicalDevice = physicalDevices[deviceIdx];
          m_VulkanParameters.m_GraphicsQueueFamilyIdx = static_cast<VulkanParameters::QueueFamilyIdx>(queueFamilyIdx);
          presentQueueFound = true;
        }

        if (queueFamilyProperties[queueFamilyIdx].queueFamilyProperties.queueFlags == VK_QUEUE_TRANSFER_BIT) {
          m_VulkanParameters.m_TransferQueueFamilyIdx = static_cast<VulkanParameters::QueueFamilyIdx>(queueFamilyIdx);
          transferQueueFound = true;
        }
      }
    }
  }

  if (!presentQueueFound) {
    std::cerr << "Could not find a suitable device with WSI surface support" << std::endl;
    return false;
  }

  if (presentQueueFound && !transferQueueFound) {
    m_VulkanParameters.m_TransferQueueFamilyIdx = m_VulkanParameters.m_GraphicsQueueFamilyIdx;
  }

  std::vector<float> const queuePriorities = { 1.0f };

  VkDeviceQueueCreateInfo queueCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                              nullptr,
                                              0,
                                              m_VulkanParameters.m_GraphicsQueueFamilyIdx,
                                              static_cast<uint32_t>(queuePriorities.size()),
                                              queuePriorities.data() };

  VkDeviceCreateInfo createInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                    nullptr,
                                    0,
                                    1,
                                    &queueCreateInfo,
                                    0,
                                    nullptr,
                                    static_cast<uint32_t>(requiredDeviceExtensions.size()),
                                    requiredDeviceExtensions.data(),
                                    nullptr };

  result = vkCreateDevice(m_VulkanParameters.m_PhysicalDevice, &createInfo, nullptr, &m_VulkanParameters.m_Device);
  if (result != VK_SUCCESS) {
    std::cerr << "Could not create logical device: " << result << std::endl;
    return false;
  }
  return true;
}

bool VulkanRenderer::LoadDeviceLevelFunctions() const
{
#ifndef VK_DEVICE_FUNCTION
#  define VK_DEVICE_FUNCTION(fun)                                                                                      \
    fun = reinterpret_cast<PFN_##fun>(vkGetDeviceProcAddr(m_VulkanParameters.m_Device, #fun));                         \
    if (!(fun)) {                                                                                                      \
      std::cerr << "Could not load device level function: " << #fun << std::endl;                                      \
      return false;                                                                                                    \
    }
#endif

#include "VulkanFunctions.inl"
  return true;
}

bool VulkanRenderer::GetSupportedSurfaceFormats(VkPhysicalDevice physicalDevice,
                                                VkSurfaceKHR surface,
                                                std::vector<VkSurfaceFormatKHR>* surfaceFormats) const
{
  uint32_t surfaceFormatsCount;
  VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatsCount, nullptr);
  if (result != VK_SUCCESS) {
    return false;
  }

  surfaceFormats->clear();
  surfaceFormats->resize(surfaceFormatsCount);

  result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &surfaceFormatsCount, surfaceFormats->data());
  if (result != VK_SUCCESS) {
    return false;
  }

  return true;
}

bool VulkanRenderer::GetSupportedPresentationModes(VkPhysicalDevice physicalDevice,
                                                   VkSurfaceKHR surface,
                                                   std::vector<VkPresentModeKHR>* presentationModes) const
{
  uint32_t presentationModesCount;
  VkResult result =
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentationModesCount, nullptr);

  if (result != VK_SUCCESS) {
    return false;
  }

  presentationModes->clear();
  presentationModes->resize(presentationModesCount);

  result = vkGetPhysicalDeviceSurfacePresentModesKHR(
    physicalDevice, surface, &presentationModesCount, presentationModes->data());

  if (result != VK_SUCCESS) {
    return false;
  }

  return true;
}

VkPresentModeKHR VulkanRenderer::GetSwapchainPresentMode(
  std::vector<VkPresentModeKHR> const& supportedPresentationModes) const
{
  auto presentModeSupported = [&](VkPresentModeKHR mode) {
    return std::find(supportedPresentationModes.cbegin(), supportedPresentationModes.cend(), mode)
           != supportedPresentationModes.cend();
  };

  if (m_VulkanParameters.m_VsyncEnabled && presentModeSupported(VK_PRESENT_MODE_MAILBOX_KHR)) {
    return VK_PRESENT_MODE_MAILBOX_KHR;
  }

  if (m_VulkanParameters.m_VsyncEnabled && presentModeSupported(VK_PRESENT_MODE_FIFO_KHR)) {
    return VK_PRESENT_MODE_FIFO_KHR;
  }

  if (!m_VulkanParameters.m_VsyncEnabled && presentModeSupported(VK_PRESENT_MODE_IMMEDIATE_KHR)) {
    return VK_PRESENT_MODE_IMMEDIATE_KHR;
  }

  return supportedPresentationModes[0];
}

uint32_t VulkanRenderer::GetSwapchainImageCount() const
{
  uint32_t imageCount = m_VulkanParameters.m_SurfaceCapabilities.minImageCount + 1;
  if (m_VulkanParameters.m_SurfaceCapabilities.maxImageCount > 0
      && imageCount > m_VulkanParameters.m_SurfaceCapabilities.maxImageCount) {
    imageCount = m_VulkanParameters.m_SurfaceCapabilities.maxImageCount;
  }

  return imageCount;
}

VkSurfaceFormatKHR VulkanRenderer::GetSwapchainFormat(
  std::vector<VkSurfaceFormatKHR> const& supportedSurfaceFormats) const
{
  if (supportedSurfaceFormats.size() == 1 && supportedSurfaceFormats[0].format == VK_FORMAT_UNDEFINED) {
    return { VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
  }

  for (auto const& surfaceFormatPair : supportedSurfaceFormats) {
    if (surfaceFormatPair.format == VK_FORMAT_R8G8B8A8_UNORM) {
      return surfaceFormatPair;
    }
  }

  return supportedSurfaceFormats[0];
}

VkExtent2D VulkanRenderer::GetSwapchainExtent() const
{
  RECT currentRect;
  GetClientRect(m_WindowParameters.m_Handle, &currentRect);
  return VkExtent2D{ static_cast<uint32_t>(currentRect.right - currentRect.left),
                     static_cast<uint32_t>(currentRect.bottom - currentRect.top) };
}

VkImageUsageFlags VulkanRenderer::GetSwapchainUsageFlags() const
{
  if (m_VulkanParameters.m_SurfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
    return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }

  std::cerr << "Could not create an usage flag bitmask with VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | "
               "VK_IMAGE_USAGE_TRANSFER_DST_BIT"
            << std::endl;
  return VK_IMAGE_USAGE_FLAG_BITS_MAX_ENUM;
}

VkSurfaceTransformFlagBitsKHR VulkanRenderer::GetSwapchainTransform() const
{
  if (m_VulkanParameters.m_SurfaceCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
    return VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  }
  return m_VulkanParameters.m_SurfaceCapabilities.currentTransform;
}

bool VulkanRenderer::CreateSwapchain()
{
  m_CanRender = false;
  if (!GetDeviceSurfaceCapabilities(m_VulkanParameters.m_PhysicalDevice,
                                    m_VulkanParameters.m_PresentSurface,
                                    &m_VulkanParameters.m_SurfaceCapabilities)) {
    std::cerr << "Could not query surface capabilities" << std::endl;
    return false;
  }

  std::vector<VkSurfaceFormatKHR> supportedSurfaceFormats{};
  if (!GetSupportedSurfaceFormats(
        m_VulkanParameters.m_PhysicalDevice, m_VulkanParameters.m_PresentSurface, &supportedSurfaceFormats)) {
    std::cerr << "Could not query supported surface formats" << std::endl;
    return false;
  }

#ifdef _DEBUG
  m_DebugOutput << "\nSupported surface format pairs: " << std::endl;
  for (decltype(supportedSurfaceFormats)::size_type idx = 0; idx != supportedSurfaceFormats.size(); ++idx) {
    if (idx != 0) {
      m_DebugOutput << std::endl;
    }
    m_DebugOutput << "\t#" << idx << " colorSpace: " << supportedSurfaceFormats[idx].colorSpace << std::endl
                  << "\t#" << idx << " format: " << supportedSurfaceFormats[idx].format << std::endl;
  }
#endif

  std::vector<VkPresentModeKHR> supportedPresentationModes{};
  if (!GetSupportedPresentationModes(
        m_VulkanParameters.m_PhysicalDevice, m_VulkanParameters.m_PresentSurface, &supportedPresentationModes)) {
    std::cerr << "Could not query the supported presentation modes" << std::endl;
  }

#ifdef _DEBUG
  m_DebugOutput << "\nSupported presentation modes: " << std::endl;
  for (decltype(supportedPresentationModes)::size_type idx = 0; idx != supportedPresentationModes.size(); ++idx) {
    m_DebugOutput << "\t#" << idx << ": " << supportedPresentationModes[idx] << std::endl;
  }
#endif

  uint32_t const desiredImageCount = GetSwapchainImageCount();
  VkSurfaceFormatKHR const desiredImageFormat = GetSwapchainFormat(supportedSurfaceFormats);
  VkExtent2D const desiredImageExtent = GetSwapchainExtent();
  VkImageUsageFlags const desiredSwapchainUsageFlags = GetSwapchainUsageFlags();
  VkSurfaceTransformFlagBitsKHR const desiredSwapchainTransform = GetSwapchainTransform();
  VkPresentModeKHR const desiredPresentationMode = GetSwapchainPresentMode(supportedPresentationModes);

#ifdef _DEBUG
  m_DebugOutput << "\nSwapchain creation setup:" << std::endl
                << "\tImage count: " << desiredImageCount << std::endl
                << "\tImage format: " << desiredImageFormat.format << std::endl
                << "\tColor space: " << desiredImageFormat.colorSpace << std::endl
                << "\tImage extent: " << VK_EXPAND_EXTENT2D(desiredImageExtent) << std::endl
                << "\tUsage flags: " << std::hex << desiredSwapchainUsageFlags << std::dec << std::endl
                << "\tSurface transform: " << desiredSwapchainTransform << std::endl
                << "\tPresentation mode: " << desiredPresentationMode << std::endl;
#endif

  // NOTE: There is a race condition here. If the OS changes the window size between the calls to
  // vkGetPhysicalDeviceSurfaceCapabilitiesKHR() and to vkCreateSwapchainKHR() the currentExtent will be invalid.
  // Either disable rendering between WM_ENTERSIZEMOVE and WM_EXITSIZEMOVE or live with this condition - not sure if
  // there is anything else we could do.
  VkSurfaceCapabilitiesKHR caps;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    m_VulkanParameters.m_PhysicalDevice, m_VulkanParameters.m_PresentSurface, &caps);
  uint32_t width = std::clamp(desiredImageExtent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
  uint32_t height = std::clamp(desiredImageExtent.height, caps.minImageExtent.height, caps.maxImageExtent.height);

  VkSwapchainCreateInfoKHR swapchainCreateInfo = {
    VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, // VkStructureType                  sType;
    nullptr,                                     // const void*                      pNext;
    0,                                           // VkSwapchainCreateFlagsKHR        flags;
    m_VulkanParameters.m_PresentSurface,         // VkSurfaceKHR                     surface;
    desiredImageCount,                           // uint32_t                         minImageCount;
    desiredImageFormat.format,                   // VkFormat                         imageFormat;
    desiredImageFormat.colorSpace,               // VkColorSpaceKHR                  imageColorSpace;
    { width, height },                           // VkExtent2D                       imageExtent;
    1,                                           // uint32_t                         imageArrayLayers;
    desiredSwapchainUsageFlags,                  // VkImageUsageFlags                imageUsage;
    VK_SHARING_MODE_EXCLUSIVE,                   // VkSharingMode                    imageSharingMode;
    0,                                           // uint32_t                         queueFamilyIndexCount;
    nullptr,                                     // const uint32_t*                  pQueueFamilyIndices;
    desiredSwapchainTransform,                   // VkSurfaceTransformFlagBitsKHR    preTransform;
    VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,           // VkCompositeAlphaFlagBitsKHR      compositeAlpha;
    desiredPresentationMode,                     // VkPresentModeKHR                 presentMode;
    VK_TRUE,                                     // VkBool32                         clipped;
    m_VulkanParameters.m_Swapchain.m_Handle      // VkSwapchainKHR                   oldSwapchain;
  };

  VkResult result = vkCreateSwapchainKHR(
    m_VulkanParameters.m_Device, &swapchainCreateInfo, nullptr, &m_VulkanParameters.m_Swapchain.m_Handle);

  if (result != VK_SUCCESS) {
    std::cerr << "Could not create the swap chain: " << result << std::endl;
  }

  uint32_t swapchainImageCount;
  result = vkGetSwapchainImagesKHR(
    m_VulkanParameters.m_Device, m_VulkanParameters.m_Swapchain.m_Handle, &swapchainImageCount, nullptr);

  if (result != VK_SUCCESS) {
    std::cerr << "Could not query swapchain image count" << std::endl;
    return false;
  }

  if (swapchainImageCount != desiredImageCount) {
    std::cerr << "Could not create the required number of swapchain images" << std::endl;
    return false;
  }

  m_VulkanParameters.m_Swapchain.m_Images.clear();
  m_VulkanParameters.m_Swapchain.m_Images.resize(swapchainImageCount);

  result = vkGetSwapchainImagesKHR(m_VulkanParameters.m_Device,
                                   m_VulkanParameters.m_Swapchain.m_Handle,
                                   &swapchainImageCount,
                                   m_VulkanParameters.m_Swapchain.m_Images.data());

  if (result != VK_SUCCESS) {
    std::cerr << "Could not create swapchain images" << std::endl;
    return false;
  }

  m_VulkanParameters.m_Swapchain.m_Format = desiredImageFormat.format;
  m_VulkanParameters.m_Swapchain.m_ImageExtent = { width, height };

  return CreateSwapchainImageViews();
}

bool VulkanRenderer::CreateSwapchainImageViews()
{
  uint32_t swapchainImagesCount = static_cast<uint32_t>(m_VulkanParameters.m_Swapchain.m_Images.size());
  m_VulkanParameters.m_Swapchain.m_ImageViews.clear();
  m_VulkanParameters.m_Swapchain.m_ImageViews.resize(swapchainImagesCount);

  VkResult result;
  for (uint32_t i = 0; i != swapchainImagesCount; ++i) {
    VkImageViewCreateInfo imageViewCreateInfo = {
      VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,   // VkStructureType            sType;
      nullptr,                                    // const void*                pNext;
      0,                                          // VkImageViewCreateFlags     flags;
      m_VulkanParameters.m_Swapchain.m_Images[i], // VkImage                    image;
      VK_IMAGE_VIEW_TYPE_2D,                      // VkImageViewType            viewType;
      m_VulkanParameters.m_Swapchain.m_Format,    // VkFormat                   format;
      VkComponentMapping{
        VK_COMPONENT_SWIZZLE_IDENTITY, // VkComponentSwizzle    r;
        VK_COMPONENT_SWIZZLE_IDENTITY, // VkComponentSwizzle    g;
        VK_COMPONENT_SWIZZLE_IDENTITY, // VkComponentSwizzle    b;
        VK_COMPONENT_SWIZZLE_IDENTITY  // VkComponentSwizzle    a;
      },                               // VkComponentMapping         components;
      VkImageSubresourceRange{
        VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT, // VkImageAspectFlags    aspectMask;
        0,                                                // uint32_t              baseMipLevel;
        1,                                                // uint32_t              levelCount;
        0,                                                // uint32_t              baseArrayLayer;
        1                                                 // uint32_t              layerCount;
      }                                                   // VkImageSubresourceRange    subresourceRange;
    };

    result = vkCreateImageView(
      m_VulkanParameters.m_Device, &imageViewCreateInfo, nullptr, &m_VulkanParameters.m_Swapchain.m_ImageViews[i]);
    if (result != VK_SUCCESS) {
      return false;
    }
  }
  m_CanRender = true;
  return true;
}

bool VulkanRenderer::CreateGraphicsQueue()
{
  vkGetDeviceQueue(
    m_VulkanParameters.m_Device, m_VulkanParameters.m_GraphicsQueueFamilyIdx, 0, &m_VulkanParameters.m_GraphicsQueue);

  if (m_VulkanParameters.m_GraphicsQueue == nullptr) {
    std::cerr << "Could not create a graphics queue" << std::endl;
    return false;
  }

  return true;
}

bool VulkanRenderer::CreateTransferQueue()
{
  vkGetDeviceQueue(
    m_VulkanParameters.m_Device, m_VulkanParameters.m_TransferQueueFamilyIdx, 0, &m_VulkanParameters.m_TransferQueue);

  if (m_VulkanParameters.m_TransferQueue == nullptr) {
    std::cerr << "Could not create a transfer queue" << std::endl;
    return false;
  }

  return true;
}

bool VulkanRenderer::CreateGraphicsCommandPool()
{
  VkCommandPoolCreateInfo commandPoolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                                    nullptr,
                                                    VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
                                                      | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
                                                    m_VulkanParameters.m_GraphicsQueueFamilyIdx };

  VkResult result = vkCreateCommandPool(
    m_VulkanParameters.m_Device, &commandPoolCreateInfo, nullptr, &m_VulkanParameters.m_GraphicsCommandPool);

  if (result != VK_SUCCESS) {
    std::cerr << "Could not create the graphics command pool" << std::endl;
    return false;
  }

  return true;
}

bool VulkanRenderer::CreateTransferCommandPool()
{
  VkCommandPoolCreateInfo commandPoolCreateInfo = {
    VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, // VkStructureType             sType;
    nullptr,                                    // const void*                 pNext;
    VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
      | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,   // VkCommandPoolCreateFlags    flags;
    m_VulkanParameters.m_TransferQueueFamilyIdx // uint32_t                    queueFamilyIndex;
  };

  VkResult result = vkCreateCommandPool(
    m_VulkanParameters.m_Device, &commandPoolCreateInfo, nullptr, &m_VulkanParameters.m_TransferCommandPool);
  if (result != VK_SUCCESS) {
    std::cerr << "Could not create transfer command pool" << std::endl;
    return false;
  }
  return true;
}


bool VulkanRenderer::AllocateGraphicsCommandBuffer(FrameResource& frameResource)
{
  VkCommandBufferAllocateInfo allocateInfo = {
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, // VkStructureType         sType;
    nullptr,                                        // const void*             pNext;
    m_VulkanParameters.m_GraphicsCommandPool,       // VkCommandPool           commandPool;
    VK_COMMAND_BUFFER_LEVEL_PRIMARY,                // VkCommandBufferLevel    level;
    1                                               // uint32_t                commandBufferCount;
  };
  VkResult result =
    vkAllocateCommandBuffers(m_VulkanParameters.m_Device, &allocateInfo, &frameResource.m_CommandBuffer);
  if (result != VK_SUCCESS) {
    std::cerr << "Could not allocate graphics command buffer" << std::endl;
    return false;
  }
  return true;
}

bool VulkanRenderer::AllocateTransferCommandBuffer()
{
  VkCommandBufferAllocateInfo allocateInfo = {
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, // VkStructureType         sType;
    nullptr,                                        // const void*             pNext;
    m_VulkanParameters.m_TransferCommandPool,       // VkCommandPool           commandPool;
    VK_COMMAND_BUFFER_LEVEL_PRIMARY,                // VkCommandBufferLevel    level;
    1                                               // uint32_t                commandBufferCount;
  };

  VkResult result =
    vkAllocateCommandBuffers(m_VulkanParameters.m_Device, &allocateInfo, &m_VulkanParameters.m_TransferCommandBuffer);
  if (result != VK_SUCCESS) {
    std::cerr << "Could not allocate transfer command buffer" << std::endl;
    return false;
  }
  return true;
}

bool VulkanRenderer::CreateFrameResources()
{
  m_FrameResources.clear();
  m_FrameResources.resize(m_FrameResourcesCount);

  for (uint32_t i = 0; i != m_FrameResources.size(); ++i) {
    if (!AllocateGraphicsCommandBuffer(m_FrameResources[i])) {
      std::cerr << "Could not allocate command buffer" << std::endl;
      return false;
    }

    if (!CreateSemaphores(m_FrameResources[i])) {
      std::cerr << "Could not create semaphores for render resource #" << i << std::endl;
      return false;
    }

    if (!CreateFence(m_FrameResources[i])) {
      std::cerr << "Could not create fence for render resource #" << i << std::endl;
      return false;
    }
  }
  return true;
}

void VulkanRenderer::FreeFrameResource(FrameResource& frameResource)
{
  if (frameResource.m_Fence) {
    vkDestroyFence(m_VulkanParameters.m_Device, frameResource.m_Fence, nullptr);
  }
  if (frameResource.m_PresentToDrawSemaphore) {
    vkDestroySemaphore(m_VulkanParameters.m_Device, frameResource.m_PresentToDrawSemaphore, nullptr);
  }
  if (frameResource.m_DrawToPresentSemaphore) {
    vkDestroySemaphore(m_VulkanParameters.m_Device, frameResource.m_DrawToPresentSemaphore, nullptr);
  }
  if (frameResource.m_Framebuffer) {
    vkDestroyFramebuffer(m_VulkanParameters.m_Device, frameResource.m_Framebuffer, nullptr);
  }
  if (frameResource.m_CommandBuffer) {
    vkFreeCommandBuffers(
      m_VulkanParameters.m_Device, m_VulkanParameters.m_GraphicsCommandPool, 1, &frameResource.m_CommandBuffer);
  }
}

bool VulkanRenderer::CreateQueryPool()
{
  VkQueryPoolCreateInfo queryPoolCreateInfo = {
    VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, // VkStructureType                  sType;
    nullptr,                                  // const void*                      pNext;
    0,                                        // VkQueryPoolCreateFlags           flags;
    VK_QUERY_TYPE_TIMESTAMP,                  // VkQueryType                      queryType;
    2,                                        // uint32_t                         queryCount;
    0                                         // VkQueryPipelineStatisticFlags    pipelineStatistics;
  };

  VkResult result =
    vkCreateQueryPool(m_VulkanParameters.m_Device, &queryPoolCreateInfo, nullptr, &m_VulkanParameters.m_QueryPool);
  if (result != VK_SUCCESS) {
    std::cerr << "Could not create query pool" << std::endl;
    return false;
  }

  return true;
}

bool VulkanRenderer::CreateSemaphores(FrameResource& frameResource)
{
  VkSemaphoreCreateInfo semaphoreCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
  VkResult result = vkCreateSemaphore(
    m_VulkanParameters.m_Device, &semaphoreCreateInfo, nullptr, &frameResource.m_PresentToDrawSemaphore);
  if (result != VK_SUCCESS) {
    std::cerr << "Could not create present to draw semaphore: " << result << std::endl;
    return false;
  }

  result = vkCreateSemaphore(
    m_VulkanParameters.m_Device, &semaphoreCreateInfo, nullptr, &frameResource.m_DrawToPresentSemaphore);
  if (result != VK_SUCCESS) {
    std::cerr << "Could not create draw to present semaphore: " << result << std::endl;
    return false;
  }
  return true;
}

bool VulkanRenderer::CreateFence(FrameResource& frameResource)
{
  VkFenceCreateInfo fenceCreateInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                        nullptr,
                                        VkFenceCreateFlagBits::VK_FENCE_CREATE_SIGNALED_BIT };
  VkResult result = vkCreateFence(m_VulkanParameters.m_Device, &fenceCreateInfo, nullptr, &frameResource.m_Fence);

  if (result != VK_SUCCESS) {
    std::cerr << "Could not create the fence!" << result << std::endl;
    return false;
  }
  return true;
}

bool VulkanRenderer::Render()
{
  uint32_t currentResourceIdx = (InterlockedIncrement(&m_CurrentResourceIdx) - 1) % m_FrameResourcesCount;
  VkResult result = vkWaitForFences(m_VulkanParameters.m_Device,
                                    1,
                                    &m_FrameResources[currentResourceIdx].m_Fence,
                                    VK_FALSE,
                                    std::numeric_limits<uint64_t>::max());
  if (result != VK_SUCCESS) {
    std::cerr << "Wait on fence timed out" << std::endl;
    return false;
  }

  uint32_t acquiredImageIdx;
  result = vkAcquireNextImageKHR(m_VulkanParameters.m_Device,
                                 m_VulkanParameters.m_Swapchain.m_Handle,
                                 std::numeric_limits<uint64_t>::max(),
                                 m_FrameResources[currentResourceIdx].m_PresentToDrawSemaphore,
                                 nullptr,
                                 &acquiredImageIdx);

  switch (result) {
  case VK_SUCCESS:
  case VK_SUBOPTIMAL_KHR:
    break;
  case VK_ERROR_OUT_OF_DATE_KHR:
#ifdef _DEBUG
    m_DebugOutput << "Swapchain image out of date during acquiring, recreating the swapchain" << std::endl;
#endif
    vkDeviceWaitIdle(m_VulkanParameters.m_Device);
    return RecreateSwapchain();
  default:
    std::cerr << "Render error! (" << result << ")" << std::endl;
    return false;
  }

  if (!PrepareAndRecordFrame(m_FrameResources[currentResourceIdx].m_CommandBuffer,
                             acquiredImageIdx,
                             m_FrameResources[currentResourceIdx].m_Framebuffer)) {
    return false;
  }

  VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
  VkSubmitInfo submitInfo = {
    VK_STRUCTURE_TYPE_SUBMIT_INFO, // VkStructureType                sType;
    nullptr,                       // const void*                    pNext;
    1,                             // uint32_t                       waitSemaphoreCount;
    &m_FrameResources[currentResourceIdx].m_PresentToDrawSemaphore, // const VkSemaphore*             pWaitSemaphores;
    &waitStageMask,                                                 // const VkPipelineStageFlags*    pWaitDstStageMask;
    1,                                                     // uint32_t                       commandBufferCount;
    &m_FrameResources[currentResourceIdx].m_CommandBuffer, // const VkCommandBuffer*         pCommandBuffers;
    1,                                                     // uint32_t                       signalSemaphoreCount;
    &m_FrameResources[currentResourceIdx].m_DrawToPresentSemaphore // const VkSemaphore* pSignalSemaphores;
  };

  vkResetFences(m_VulkanParameters.m_Device, 1, &m_FrameResources[currentResourceIdx].m_Fence);
  result =
    vkQueueSubmit(m_VulkanParameters.m_GraphicsQueue, 1, &submitInfo, m_FrameResources[currentResourceIdx].m_Fence);
  if (result != VK_SUCCESS) {
    std::cerr << "Error while submitting commands to the present queue" << std::endl;
    return false;
  }

  VkPresentInfoKHR presentInfo = {
    VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,                             // VkStructureType          sType;
    nullptr,                                                        // const void*              pNext;
    1,                                                              // uint32_t                 waitSemaphoreCount;
    &m_FrameResources[currentResourceIdx].m_DrawToPresentSemaphore, // const VkSemaphore*       pWaitSemaphores;
    1,                                                              // uint32_t                 swapchainCount;
    &m_VulkanParameters.m_Swapchain.m_Handle,                       // const VkSwapchainKHR*    pSwapchains;
    &acquiredImageIdx,                                              // const uint32_t*          pImageIndices;
    nullptr                                                         // VkResult*                pResults;
  };

  result = vkQueuePresentKHR(m_VulkanParameters.m_GraphicsQueue, &presentInfo);
  switch (result) {
  case VK_SUCCESS:
    break;
  case VK_ERROR_OUT_OF_DATE_KHR:
  case VK_SUBOPTIMAL_KHR:
#ifdef _DEBUG
    m_DebugOutput << "Swapchain image suboptimal or out of date during presenting, recreating swapchain" << std::endl;
#endif
    vkDeviceWaitIdle(m_VulkanParameters.m_Device);
    return RecreateSwapchain();
  default:
    std::cerr << "Render error! (" << result << ")" << std::endl;
    return false;
  }

  // Frame time data
  m_FrameStat.m_BeginFrameTimestamp = 0;
  m_FrameStat.m_EndFrameTimestamp = 0;
  result = vkGetQueryPoolResults(m_VulkanParameters.m_Device,
                                 m_VulkanParameters.m_QueryPool,
                                 0,
                                 2,
                                 2 * sizeof(uint64_t),
                                 &m_FrameStat,
                                 sizeof(uint64_t),
                                 VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

  if (result != VK_SUCCESS) {
    std::cerr << "Could not query frame timestamps" << std::endl;
    return false;
  }
  double frameTimeInMs = static_cast<double>(m_FrameStat.m_EndFrameTimestamp - m_FrameStat.m_BeginFrameTimestamp)
                         * static_cast<double>(m_VulkanParameters.m_TimestampPeriod) / 1'000'000.0;

  double fps = 1.0 / (frameTimeInMs / 1'000);
  UNREFERENCED_PARAMETER(fps);
//  std::cout << "GPU time: " << frameTimeInMs << " ms (" << fps << " fps)" << std::endl;
  return true;
}

bool VulkanRenderer::CreateRenderPass()
{
  std::vector<VkAttachmentDescription> attachments = { VkAttachmentDescription{
    0,                                       // VkAttachmentDescriptionFlags    flags;
    m_VulkanParameters.m_Swapchain.m_Format, // VkFormat                        format;
    VK_SAMPLE_COUNT_1_BIT,                   // VkSampleCountFlagBits           samples;
    VK_ATTACHMENT_LOAD_OP_CLEAR,             // VkAttachmentLoadOp              loadOp;
    VK_ATTACHMENT_STORE_OP_STORE,            // VkAttachmentStoreOp             storeOp;
    VK_ATTACHMENT_LOAD_OP_DONT_CARE,         // VkAttachmentLoadOp              stencilLoadOp;
    VK_ATTACHMENT_STORE_OP_DONT_CARE,        // VkAttachmentStoreOp             stencilStoreOp;
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,         // VkImageLayout                   initialLayout;
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR          // VkImageLayout                   finalLayout;
  } };

  std::vector<VkAttachmentReference> colorAttachments = { VkAttachmentReference{
    0,                                       // uint32_t         attachment;
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL // VkImageLayout    layout;
  } };

  std::vector<VkSubpassDescription> subpasses = { VkSubpassDescription{
    0,                                              // VkSubpassDescriptionFlags       flags;
    VK_PIPELINE_BIND_POINT_GRAPHICS,                // VkPipelineBindPoint             pipelineBindPoint;
    0,                                              // uint32_t                        inputAttachmentCount;
    nullptr,                                        // const VkAttachmentReference*    pInputAttachments;
    static_cast<uint32_t>(colorAttachments.size()), // uint32_t                        colorAttachmentCount;
    colorAttachments.data(),                        // const VkAttachmentReference*    pColorAttachments;
    nullptr,                                        // const VkAttachmentReference*    pResolveAttachments;
    nullptr,                                        // const VkAttachmentReference*    pDepthStencilAttachment;
    0,                                              // uint32_t                        preserveAttachmentCount;
    nullptr                                         // const uint32_t*                 pPreserveAttachments;
  } };

  std::vector<VkSubpassDependency> subpassDependencies = {
    VkSubpassDependency{
      VK_SUBPASS_EXTERNAL,                           // uint32_t                srcSubpass;
      0,                                             // uint32_t                dstSubpass;
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,          // VkPipelineStageFlags    srcStageMask;
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // VkPipelineStageFlags    dstStageMask;
      VK_ACCESS_MEMORY_READ_BIT,                     // VkAccessFlags           srcAccessMask;
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,          // VkAccessFlags           dstAccessMask;
      VK_DEPENDENCY_BY_REGION_BIT                    // VkDependencyFlags       dependencyFlags;
    },
    VkSubpassDependency{
      0,                                             // uint32_t                srcSubpass;
      VK_SUBPASS_EXTERNAL,                           // uint32_t                dstSubpass;
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // VkPipelineStageFlags    srcStageMask;
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,          // VkPipelineStageFlags    dstStageMask;
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,          // VkAccessFlags           srcAccessMask;
      VK_ACCESS_MEMORY_READ_BIT,                     // VkAccessFlags           dstAccessMask;
      VK_DEPENDENCY_BY_REGION_BIT                    // VkDependencyFlags       dependencyFlags;
    }
  };

  VkRenderPassCreateInfo renderPassCreateInfo = {
    VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,         // VkStructureType                   sType;
    nullptr,                                           // const void*                       pNext;
    0,                                                 // VkRenderPassCreateFlags           flags;
    static_cast<uint32_t>(attachments.size()),         // uint32_t                          attachmentCount;
    attachments.data(),                                // const VkAttachmentDescription*    pAttachments;
    static_cast<uint32_t>(subpasses.size()),           // uint32_t                          subpassCount;
    subpasses.data(),                                  // const VkSubpassDescription*       pSubpasses;
    static_cast<uint32_t>(subpassDependencies.size()), // uint32_t                          dependencyCount;
    subpassDependencies.data()                         // const VkSubpassDependency*        pDependencies;
  };

  VkResult const result =
    vkCreateRenderPass(m_VulkanParameters.m_Device, &renderPassCreateInfo, nullptr, &m_VulkanParameters.m_RenderPass);

  if (result != VK_SUCCESS) {
    std::cerr << "Could not create render pass instance: " << result << std::endl;
    return false;
  }
  return true;
}

bool VulkanRenderer::CreatePipeline()
{
  auto vertexShaderModule = CreateShaderModule("shaders/shader.vert.spv");
  if (!vertexShaderModule) {
    return false;
  }

  auto fragmentShaderModule = CreateShaderModule("shaders/shader.frag.spv");
  if (!fragmentShaderModule) {
    return false;
  }

  std::vector<VkPipelineShaderStageCreateInfo> shaderStages = {
    {
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, // VkStructureType                     sType;
      nullptr,                                             // const void*                         pNext;
      0,                                                   // VkPipelineShaderStageCreateFlags    flags;
      VK_SHADER_STAGE_VERTEX_BIT,                          // VkShaderStageFlagBits               stage;
      vertexShaderModule.Get(),                            // VkShaderModule                      module;
      "main",                                              // const char*                         pName;
      nullptr                                              // const VkSpecializationInfo*         pSpecializationInfo;
    },
    {
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, // VkStructureType                     sType;
      nullptr,                                             // const void*                         pNext;
      0,                                                   // VkPipelineShaderStageCreateFlags    flags;
      VK_SHADER_STAGE_FRAGMENT_BIT,                        // VkShaderStageFlagBits               stage;
      fragmentShaderModule.Get(),                          // VkShaderModule                      module;
      "main",                                              // const char*                         pName;
      nullptr                                              // const VkSpecializationInfo*         pSpecializationInfo;
    }
  };

  std::vector<VkVertexInputBindingDescription> vertexBindingDescriptions = { {
    0,                          // uint32_t             binding;
    sizeof(VertexData),         // uint32_t             stride;
    VK_VERTEX_INPUT_RATE_VERTEX // VkVertexInputRate    inputRate;
  } };

  std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
    {
      0,                                           // uint32_t    location;
      vertexBindingDescriptions[0].binding,        // uint32_t    binding;
      VkFormat::VK_FORMAT_R32G32B32A32_SFLOAT,     // VkFormat    format;
      offsetof(VertexData, VertexData::m_Position) // uint32_t    offset;
    },
    {
      1,                                        // uint32_t    location;
      vertexBindingDescriptions[0].binding,     // uint32_t    binding;
      VkFormat::VK_FORMAT_R32G32B32A32_SFLOAT,  // VkFormat    format;
      offsetof(VertexData, VertexData::m_Color) // uint32_t    offset;
    }
  };

  VkPipelineVertexInputStateCreateInfo vertexInputState = {
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, // VkStructureType                             sType;
    nullptr,                                                   // const void*                                 pNext;
    0,                                                         // VkPipelineVertexInputStateCreateFlags       flags;
    static_cast<uint32_t>(vertexBindingDescriptions.size()),   // uint32_t vertexBindingDescriptionCount;
    vertexBindingDescriptions.data(), // const VkVertexInputBindingDescription*      pVertexBindingDescriptions;
    static_cast<uint32_t>(vertexInputAttributes.size()), // uint32_t vertexAttributeDescriptionCount;
    vertexInputAttributes.data() // const VkVertexInputAttributeDescription*    pVertexAttributeDescriptions;
  };

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {
    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    nullptr,
    0,
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
    VK_FALSE
  };

  VkPipelineViewportStateCreateInfo viewportState = {
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, // VkStructureType                       sType;
    nullptr,                                               // const void*                           pNext;
    0,                                                     // VkPipelineViewportStateCreateFlags    flags;
    1,                                                     // uint32_t                              viewportCount;
    nullptr,                                               // const VkViewport*                     pViewports;
    1,                                                     // uint32_t                              scissorCount;
    nullptr                                                // const VkRect2D*                       pScissors;
  };

  VkPipelineRasterizationStateCreateInfo rasterizationState = {
    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, // VkStructureType                            sType;
    nullptr,                                                    // const void*                                pNext;
    0,                                                          // VkPipelineRasterizationStateCreateFlags    flags;
    VK_FALSE,                        // VkBool32                                   depthClampEnable;
    VK_FALSE,                        // VkBool32                                   rasterizerDiscardEnable;
    VK_POLYGON_MODE_FILL,            // VkPolygonMode                              polygonMode;
    VK_CULL_MODE_BACK_BIT,           // VkCullModeFlags                            cullMode;
    VK_FRONT_FACE_COUNTER_CLOCKWISE, // VkFrontFace                                frontFace;
    VK_FALSE,                        // VkBool32                                   depthBiasEnable;
    0.0f,                            // float                                      depthBiasConstantFactor;
    0.0f,                            // float                                      depthBiasClamp;
    0.0f,                            // float                                      depthBiasSlopeFactor;
    1.0f                             // float                                      lineWidth;
  };

  VkPipelineMultisampleStateCreateInfo multisampleState = {
    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, // VkStructureType                          sType;
    nullptr,                                                  // const void*                              pNext;
    0,                                                        // VkPipelineMultisampleStateCreateFlags    flags;
    VK_SAMPLE_COUNT_1_BIT, // VkSampleCountFlagBits                    rasterizationSamples;
    VK_FALSE,              // VkBool32                                 sampleShadingEnable;
    1.0f,                  // float                                    minSampleShading;
    nullptr,               // const VkSampleMask*                      pSampleMask;
    VK_FALSE,              // VkBool32                                 alphaToCoverageEnable;
    VK_FALSE               // VkBool32                                 alphaToOneEnable;
  };

  VkPipelineColorBlendAttachmentState colorBlendAttachmentState = {
    VK_FALSE,             // VkBool32                 blendEnable;
    VK_BLEND_FACTOR_ONE,  // VkBlendFactor            srcColorBlendFactor;
    VK_BLEND_FACTOR_ZERO, // VkBlendFactor            dstColorBlendFactor;
    VK_BLEND_OP_ADD,      // VkBlendOp                colorBlendOp;
    VK_BLEND_FACTOR_ONE,  // VkBlendFactor            srcAlphaBlendFactor;
    VK_BLEND_FACTOR_ZERO, // VkBlendFactor            dstAlphaBlendFactor;
    VK_BLEND_OP_ADD,      // VkBlendOp                alphaBlendOp;
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
      | VK_COLOR_COMPONENT_A_BIT // VkColorComponentFlags    colorWriteMask;
  };

  VkPipelineColorBlendStateCreateInfo colorBlendState = {
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, // VkStructureType                               sType;
    nullptr,                                                  // const void*                                   pNext;
    0,                                                        // VkPipelineColorBlendStateCreateFlags          flags;
    VK_FALSE,                   // VkBool32                                      logicOpEnable;
    VK_LOGIC_OP_COPY,           // VkLogicOp                                     logicOp;
    1,                          // uint32_t                                      attachmentCount;
    &colorBlendAttachmentState, // const VkPipelineColorBlendAttachmentState*    pAttachments;
    { 0.0f, 0.0f, 0.0f, 0.0f }  // float                                         blendConstants[4];
  };

  std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

  VkPipelineDynamicStateCreateInfo dynamicState = {
    VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, // VkStructureType                      sType;
    nullptr,                                              // const void*                          pNext;
    0,                                                    // VkPipelineDynamicStateCreateFlags    flags;
    static_cast<uint32_t>(dynamicStates.size()),          // uint32_t                             dynamicStateCount;
    dynamicStates.data()                                  // const VkDynamicState*                pDynamicStates;
  };

  VulkanDeleter<VkPipelineLayout, PFN_vkDestroyPipelineLayout> layout = CreatePipelineLayout();
  if (!layout) {
    std::cerr << "Could not create pipeline layout" << std::endl;
    return false;
  }

  VkGraphicsPipelineCreateInfo pipelineCreateInfo = {
    VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, // VkStructureType                                  sType;
    nullptr,                                         // const void*                                      pNext;
    0,                                               // VkPipelineCreateFlags                            flags;
    static_cast<uint32_t>(shaderStages.size()),      // uint32_t                                         stageCount;
    shaderStages.data(),                             // const VkPipelineShaderStageCreateInfo*           pStages;
    &vertexInputState,               // const VkPipelineVertexInputStateCreateInfo*      pVertexInputState;
    &inputAssemblyState,             // const VkPipelineInputAssemblyStateCreateInfo*    pInputAssemblyState;
    nullptr,                         // const VkPipelineTessellationStateCreateInfo*     pTessellationState;
    &viewportState,                  // const VkPipelineViewportStateCreateInfo*         pViewportState;
    &rasterizationState,             // const VkPipelineRasterizationStateCreateInfo*    pRasterizationState;
    &multisampleState,               // const VkPipelineMultisampleStateCreateInfo*      pMultisampleState;
    nullptr,                         // const VkPipelineDepthStencilStateCreateInfo*     pDepthStencilState;
    &colorBlendState,                // const VkPipelineColorBlendStateCreateInfo*       pColorBlendState;
    &dynamicState,                   // const VkPipelineDynamicStateCreateInfo*          pDynamicState;
    layout.Get(),                    // VkPipelineLayout                                 layout;
    m_VulkanParameters.m_RenderPass, // VkRenderPass                                     renderPass;
    0,                               // uint32_t                                         subpass;
    nullptr,                         // VkPipeline                                       basePipelineHandle;
    -1                               // int32_t                                          basePipelineIndex;
  };

  VkResult result = vkCreateGraphicsPipelines(
    m_VulkanParameters.m_Device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &m_VulkanParameters.m_Pipeline);
  if (result != VK_SUCCESS) {
    std::cerr << "Could not create graphics pipeline!" << std::endl;
    return false;
  }

  return true;
}

std::vector<char> VulkanRenderer::ReadShaderContent(char const* filename)
{
  std::vector<char> shaderContent;
  std::filesystem::path shaderPath = Os::GetExecutableDirectory() / filename;
  std::ifstream file(shaderPath, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    std::cerr << "Could not open shader file: " << shaderPath << std::endl;
    return shaderContent;
  }

  size_t codeSize = file.tellg();
  shaderContent.resize(codeSize);
  file.seekg(0);
  file.read(shaderContent.data(), codeSize);
  file.close();
  return shaderContent;
}

VulkanDeleter<VkShaderModule, PFN_vkDestroyShaderModule> VulkanRenderer::CreateShaderModule(char const* filename)
{
  std::vector<char> code = ReadShaderContent(filename);
  if (code.empty()) {
    std::cerr << "Could not read shader file or its empty: " << filename << std::endl;
    return VulkanDeleter<VkShaderModule, PFN_vkDestroyShaderModule>();
  }

  VkShaderModule shaderModule;
  VkShaderModuleCreateInfo shaderModuleCreateInfo = {
    VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, code.size(), reinterpret_cast<uint32_t const*>(code.data())
  };

  VkResult result = vkCreateShaderModule(m_VulkanParameters.m_Device, &shaderModuleCreateInfo, nullptr, &shaderModule);
  if (result != VK_SUCCESS) {
    return VulkanDeleter<VkShaderModule, PFN_vkDestroyShaderModule>();
  }

  return VulkanDeleter<VkShaderModule, PFN_vkDestroyShaderModule>(
    shaderModule, vkDestroyShaderModule, m_VulkanParameters.m_Device);
}

VulkanDeleter<VkPipelineLayout, PFN_vkDestroyPipelineLayout> VulkanRenderer::CreatePipelineLayout()
{
  VkPipelineLayout pipelineLayout;

  VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, // VkStructureType                 sType;
    nullptr,                                       // const void*                     pNext;
    0,                                             // VkPipelineLayoutCreateFlags     flags;
    0,                                             // uint32_t                        setLayoutCount;
    nullptr,                                       // const VkDescriptorSetLayout*    pSetLayouts;
    0,                                             // uint32_t                        pushConstantRangeCount;
    nullptr                                        // const VkPushConstantRange*      pPushConstantRanges;
  };
  VkResult result =
    vkCreatePipelineLayout(m_VulkanParameters.m_Device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout);
  if (result != VK_SUCCESS) {
    return VulkanDeleter<VkPipelineLayout, PFN_vkDestroyPipelineLayout>();
  }
  return VulkanDeleter<VkPipelineLayout, PFN_vkDestroyPipelineLayout>(
    pipelineLayout, vkDestroyPipelineLayout, m_VulkanParameters.m_Device);
}


bool VulkanRenderer::RecreateSwapchain()
{
  if (vkDeviceWaitIdle(m_VulkanParameters.m_Device) != VK_SUCCESS) {
    std::cerr << "Error while waiting for device idle" << std::endl;
    return false;
  }

  for (auto& imageView : m_VulkanParameters.m_Swapchain.m_ImageViews) {
    vkDestroyImageView(m_VulkanParameters.m_Device, imageView, nullptr);
  }

  if (m_VulkanParameters.m_Swapchain.m_Handle) {
    vkDestroySwapchainKHR(m_VulkanParameters.m_Device, m_VulkanParameters.m_Swapchain.m_Handle, nullptr);
    m_VulkanParameters.m_Swapchain.m_Handle = nullptr;
  }

  if (!CreateSwapchain()) {
    return false;
  }
  return true;
}

bool VulkanRenderer::CreateBuffer(BufferData& buffer,
                                  VkBufferUsageFlags usage,
                                  VkMemoryPropertyFlags requiredProperties)
{
  VkBufferCreateInfo bufferCreateInfo = {
    VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, // VkStructureType        sType;
    nullptr,                              // const void*            pNext;
    0,                                    // VkBufferCreateFlags    flags;
    buffer.m_Size,                        // VkDeviceSize           size;
    usage,                                // VkBufferUsageFlags     usage;
    VK_SHARING_MODE_EXCLUSIVE,            // VkSharingMode          sharingMode;
    0,                                    // uint32_t               queueFamilyIndexCount;
    nullptr                               // const uint32_t*        pQueueFamilyIndices;
  };

  VkResult result = vkCreateBuffer(m_VulkanParameters.m_Device, &bufferCreateInfo, nullptr, &buffer.m_Handle);
  if (result != VK_SUCCESS) {
    std::cerr << "Could not create buffer" << std::endl;
    return false;
  }

  if (!AllocateBuffer(buffer, requiredProperties)) {
    std::cerr << "Could not allocate buffer" << std::endl;
    return false;
  }

  result = vkBindBufferMemory(m_VulkanParameters.m_Device, buffer.m_Handle, buffer.m_Memory, 0);
  if (result != VK_SUCCESS) {
    std::cerr << "Could not bind buffer to memory" << std::endl;
    return false;
  }

  return true;
}

bool VulkanRenderer::AllocateBuffer(BufferData& buffer, VkMemoryPropertyFlags requiredProperties)
{
  VkMemoryRequirements memoryRequirements;
  vkGetBufferMemoryRequirements(m_VulkanParameters.m_Device, buffer.m_Handle, &memoryRequirements);

  VkPhysicalDeviceMemoryProperties memoryProperties;
  vkGetPhysicalDeviceMemoryProperties(m_VulkanParameters.m_PhysicalDevice, &memoryProperties);

  VkResult result;
  for (uint32_t i = 0; i != memoryProperties.memoryTypeCount; ++i) {
    if (memoryRequirements.memoryTypeBits & (1 << i)
        && (memoryProperties.memoryTypes[i].propertyFlags & requiredProperties)) {
      VkMemoryAllocateInfo allocateInfo = {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, // VkStructureType    sType;
        nullptr,                                // const void*        pNext;
        memoryRequirements.size,                // VkDeviceSize       allocationSize;
        i                                       // uint32_t           memoryTypeIndex;
      };
      result = vkAllocateMemory(m_VulkanParameters.m_Device, &allocateInfo, nullptr, &buffer.m_Memory);
      if (result == VK_SUCCESS) {
        return true;
      }
    }
  }
  return false;
}

void VulkanRenderer::FreeBuffer(BufferData& buffer)
{
  vkDeviceWaitIdle(m_VulkanParameters.m_Device);
  if (buffer.m_Memory) {
    vkFreeMemory(m_VulkanParameters.m_Device, buffer.m_Memory, nullptr);
  }
  if (buffer.m_Handle) {
    vkDestroyBuffer(m_VulkanParameters.m_Device, buffer.m_Handle, nullptr);
  }
}

bool VulkanRenderer::CreateFramebuffer(VkFramebuffer& framebuffer, VkImageView& imageView)
{
  if (framebuffer) {
    vkDestroyFramebuffer(m_VulkanParameters.m_Device, framebuffer, nullptr);
    framebuffer = nullptr;
  }
  VkFramebufferCreateInfo frameBufferCreateInfo = {
    VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,           // VkStructureType             sType;
    nullptr,                                             // const void*                 pNext;
    0,                                                   // VkFramebufferCreateFlags    flags;
    m_VulkanParameters.m_RenderPass,                     // VkRenderPass                renderPass;
    1,                                                   // uint32_t                    attachmentCount;
    &imageView,                                          // const VkImageView*          pAttachments;
    m_VulkanParameters.m_Swapchain.m_ImageExtent.width,  // uint32_t                    width;
    m_VulkanParameters.m_Swapchain.m_ImageExtent.height, // uint32_t                    height;
    1                                                    // uint32_t                    layers;
  };
  VkResult result = vkCreateFramebuffer(m_VulkanParameters.m_Device, &frameBufferCreateInfo, nullptr, &framebuffer);
  if (result != VK_SUCCESS) {
    return false;
  }
  return true;
}


bool VulkanRenderer::PrepareAndRecordFrame(VkCommandBuffer commandBuffer,
                                           uint32_t acquiredImageIdx,
                                           VkFramebuffer& framebuffer)
{
  if (!CreateFramebuffer(framebuffer, m_VulkanParameters.m_Swapchain.m_ImageViews[acquiredImageIdx])) {
    return false;
  }

  VkCommandBufferBeginInfo commandBufferBeginInfo = {
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, // VkStructureType                          sType;
    nullptr,                                     // const void*                              pNext;
    VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, // VkCommandBufferUsageFlags                flags;
    nullptr                                      // const VkCommandBufferInheritanceInfo*    pInheritanceInfo;
  };

  vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo);
  vkCmdResetQueryPool(commandBuffer, m_VulkanParameters.m_QueryPool, 0, 2);
  vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_VulkanParameters.m_QueryPool, 0);

  VkImageSubresourceRange subresourceRange = {
    VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT, // VkImageAspectFlags    aspectMask;
    0,                                                // uint32_t              baseMipLevel;
    1,                                                // uint32_t              levelCount;
    0,                                                // uint32_t              baseArrayLayer;
    1                                                 // uint32_t              layerCount;
  };

  VkImageMemoryBarrier fromPresentToDrawBarrier = {
    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,                    // VkStructureType            sType;
    nullptr,                                                   // const void*                pNext;
    VK_ACCESS_MEMORY_READ_BIT,                                 // VkAccessFlags              srcAccessMask;
    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,                      // VkAccessFlags              dstAccessMask;
    VK_IMAGE_LAYOUT_UNDEFINED,                                 // VkImageLayout              oldLayout;
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,                           // VkImageLayout              newLayout;
    m_VulkanParameters.m_GraphicsQueueFamilyIdx,               // uint32_t                   srcQueueFamilyIndex;
    m_VulkanParameters.m_GraphicsQueueFamilyIdx,               // uint32_t                   dstQueueFamilyIndex;
    m_VulkanParameters.m_Swapchain.m_Images[acquiredImageIdx], // VkImage                    image;
    subresourceRange                                           // VkImageSubresourceRange    subresourceRange;
  };

  vkCmdPipelineBarrier(commandBuffer,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       0,
                       0,
                       nullptr,
                       0,
                       nullptr,
                       1,
                       &fromPresentToDrawBarrier);
  // Core
  ImageData imageData = { m_VulkanParameters.m_Swapchain.m_ImageViews[acquiredImageIdx],
                          m_VulkanParameters.m_Swapchain.m_ImageExtent.width,
                          m_VulkanParameters.m_Swapchain.m_ImageExtent.height };

  if (m_OnRenderFrameCallback && !m_OnRenderFrameCallback(commandBuffer, framebuffer, imageData)) {
    return false;
  }

  VkImageMemoryBarrier fromDrawToPresentBarrier = {
    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,                    // VkStructureType            sType;
    nullptr,                                                   // const void*                pNext;
    VkAccessFlagBits::VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,    // VkAccessFlags              srcAccessMask;
    VkAccessFlagBits::VK_ACCESS_MEMORY_READ_BIT,               // VkAccessFlags              dstAccessMask;
    VkImageLayout::VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,            // VkImageLayout              oldLayout;
    VkImageLayout::VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,            // VkImageLayout              newLayout;
    m_VulkanParameters.m_GraphicsQueueFamilyIdx,               // uint32_t                   srcQueueFamilyIndex;
    m_VulkanParameters.m_GraphicsQueueFamilyIdx,               // uint32_t                   dstQueueFamilyIndex;
    m_VulkanParameters.m_Swapchain.m_Images[acquiredImageIdx], // VkImage                    image;
    subresourceRange                                           // VkImageSubresourceRange    subresourceRange;
  };

  vkCmdPipelineBarrier(commandBuffer,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                       0,
                       0,
                       nullptr,
                       0,
                       nullptr,
                       1,
                       &fromDrawToPresentBarrier);

  vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_VulkanParameters.m_QueryPool, 1);
  vkEndCommandBuffer(commandBuffer);
  return true;
}

} // namespace Core