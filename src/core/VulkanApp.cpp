#include "VulkanApp.h"

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
  m_PresentQueueFamilyIdx(std::numeric_limits<QueueFamilyIdx>::max()),
  m_Device(nullptr),
  m_Queue(nullptr),
  m_PresentSurface(nullptr),
  m_SurfaceCapabilities(),
  m_Swapchain(SwapchainData()),
  m_PresentCommandPool(nullptr),
  m_PresentCommandBuffers(std::vector<VkCommandBuffer>()),
  m_VsyncEnabled(false),
  m_RenderPass(nullptr),
  m_FramebufferObjects(std::vector<FrameBufferObjects>()),
  m_Pipeline(nullptr)
{}

VulkanApp::VulkanApp(bool vsyncEnabled) : VulkanApp(std::cout, vsyncEnabled)
{}

VulkanApp::VulkanApp(std::ostream& debugOutput, bool vsyncEnabled) :
  m_VulkanLoaderHandle(nullptr),
  m_VulkanParameters(VulkanParameters()),
  m_DebugOutput(debugOutput),
  m_WindowParameters(Os::WindowParameters())
{
  m_DebugOutput << std::showbase;
  m_VulkanParameters.m_VsyncEnabled = vsyncEnabled;
}

void VulkanApp::Free()
{
  if (m_VulkanParameters.m_Device) {
    vkDeviceWaitIdle(m_VulkanParameters.m_Device);
    FreeSwapchainAndRenderResources();

    vkDestroyDevice(m_VulkanParameters.m_Device, nullptr);
    m_VulkanParameters.m_Device = nullptr;
    m_VulkanParameters.m_Queue = nullptr;
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

VulkanApp::~VulkanApp()
{
  Free();
}

bool VulkanApp::PrepareVulkan(Os::WindowParameters windowParameters)
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

  if (!CreateQueue()) {
    return false;
  }

  return true;
}

bool VulkanApp::LoadVulkanLibrary()
{
#ifdef VK_USE_PLATFORM_WIN32_KHR
  m_VulkanLoaderHandle = LoadLibrary(L"vulkan-1.dll");
#endif
  return m_VulkanLoaderHandle != nullptr;
}

bool VulkanApp::LoadExportedEntryPoints() const
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

bool VulkanApp::LoadGlobalLevelFunctions() const
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

void VulkanApp::GetVulkanImplementationVersion(uint32_t* implementationVersion) const
{
  vkEnumerateInstanceVersion(implementationVersion);
}

bool VulkanApp::GetVulkanLayers(std::vector<VkLayerProperties>* layers) const
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

bool VulkanApp::GetVulkanInstanceExtensions(std::vector<VkExtensionProperties>* instanceExtensions) const
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

bool VulkanApp::RequiredInstanceExtensionsAvailable(std::vector<char const*> const& requiredExtensions) const
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

bool VulkanApp::GetVulkanLayerExtensions(char const* layerName,
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

bool VulkanApp::LoadInstanceLevelFunctions() const
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

bool VulkanApp::CreateInstance(std::vector<char const*> const& requiredExtensions)
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

bool VulkanApp::CreatePresentationSurface()
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

bool VulkanApp::GetVulkanDeviceExtensions(VkPhysicalDevice physicalDevice,
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

bool VulkanApp::RequiredDeviceExtensionsAvailable(VkPhysicalDevice physicalDevice,
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

bool VulkanApp::GetDeviceSurfaceCapabilities(VkPhysicalDevice physicalDevice,
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

bool VulkanApp::CreateDevice()
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
                  << deviceProperties[deviceIdx].properties.limits.framebufferDepthSampleCounts << std::dec
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
            && m_VulkanParameters.m_PhysicalDevice == nullptr && isSurfacePresentationSupported == VK_TRUE) {
          m_VulkanParameters.m_PhysicalDevice = physicalDevices[deviceIdx];
          m_VulkanParameters.m_PresentQueueFamilyIdx = static_cast<VulkanParameters::QueueFamilyIdx>(queueFamilyIdx);
        }
      }
    }
  }

  if (m_VulkanParameters.m_PhysicalDevice == nullptr) {
    std::cerr << "Could not find a suitable device with WSI surface support" << std::endl;
    return false;
  }

  std::vector<float> const queuePriorities = { 1.0f };

  VkDeviceQueueCreateInfo queueCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                              nullptr,
                                              0,
                                              m_VulkanParameters.m_PresentQueueFamilyIdx,
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

bool VulkanApp::LoadDeviceLevelFunctions() const
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

bool VulkanApp::GetSupportedSurfaceFormats(VkPhysicalDevice physicalDevice,
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

bool VulkanApp::GetSupportedPresentationModes(VkPhysicalDevice physicalDevice,
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

VkPresentModeKHR VulkanApp::GetSwapchainPresentMode(
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

uint32_t VulkanApp::GetSwapchainImageCount() const
{
  uint32_t imageCount = m_VulkanParameters.m_SurfaceCapabilities.minImageCount + 1;
  if (m_VulkanParameters.m_SurfaceCapabilities.maxImageCount > 0
      && imageCount > m_VulkanParameters.m_SurfaceCapabilities.maxImageCount) {
    imageCount = m_VulkanParameters.m_SurfaceCapabilities.maxImageCount;
  }

  return imageCount;
}

VkSurfaceFormatKHR VulkanApp::GetSwapchainFormat(std::vector<VkSurfaceFormatKHR> const& supportedSurfaceFormats) const
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

VkExtent2D VulkanApp::GetSwapchainExtent() const
{
  RECT currentRect;
  GetClientRect(m_WindowParameters.m_Handle, &currentRect);
  return VkExtent2D{ static_cast<uint32_t>(currentRect.right - currentRect.left),
                     static_cast<uint32_t>(currentRect.bottom - currentRect.top) };
}

VkImageUsageFlags VulkanApp::GetSwapchainUsageFlags() const
{
  if (m_VulkanParameters.m_SurfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
    return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }

  std::cerr << "Could not create an usage flag bitmask with VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | "
               "VK_IMAGE_USAGE_TRANSFER_DST_BIT"
            << std::endl;
  return VK_IMAGE_USAGE_FLAG_BITS_MAX_ENUM;
}

VkSurfaceTransformFlagBitsKHR VulkanApp::GetSwapchainTransform() const
{
  if (m_VulkanParameters.m_SurfaceCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
    return VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  }
  return m_VulkanParameters.m_SurfaceCapabilities.currentTransform;
}

bool VulkanApp::CreateSwapchainAndRenderResources()
{
  if (!CreateSwapchain()) {
    return false;
  }

  if (!CreateCommandBuffers()) {
    return false;
  }

  if (!CreateRenderPass()) {
    return false;
  }

  if (!CreateFramebuffers()) {
    return false;
  }

  if (!CreatePipeline()) {
    return false;
  }

  if (!RecordCommandBuffers()) {
    std::cerr << "Could not record command present buffers" << std::endl;
    return false;
  }

  return true;
}

bool VulkanApp::CreateSwapchain()
{
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

  return true;
}

bool VulkanApp::CreateQueue()
{
  vkGetDeviceQueue(
    m_VulkanParameters.m_Device, m_VulkanParameters.m_PresentQueueFamilyIdx, 0, &m_VulkanParameters.m_Queue);

  if (m_VulkanParameters.m_Queue == nullptr) {
    std::cerr << "Could not create a queue" << std::endl;
    return false;
  }

  return true;
}

bool VulkanApp::CreateCommandBuffers()
{
  VkCommandPoolCreateInfo commandPoolCreateInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                                    nullptr,
                                                    VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                                    m_VulkanParameters.m_PresentQueueFamilyIdx };

  VkResult result = vkCreateCommandPool(
    m_VulkanParameters.m_Device, &commandPoolCreateInfo, nullptr, &m_VulkanParameters.m_PresentCommandPool);

  if (result != VK_SUCCESS) {
    std::cerr << "Could not create the presentation command pool" << std::endl;
    return false;
  }

  uint32_t commandBufferCount = static_cast<uint32_t>(m_VulkanParameters.m_Swapchain.m_Images.size());
  assert(commandBufferCount != 0);
  m_VulkanParameters.m_PresentCommandBuffers.clear();
  m_VulkanParameters.m_PresentCommandBuffers.resize(commandBufferCount);

  VkCommandBufferAllocateInfo allocateInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                               nullptr,
                                               m_VulkanParameters.m_PresentCommandPool,
                                               VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                               static_cast<uint32_t>(commandBufferCount) };

  result = vkAllocateCommandBuffers(
    m_VulkanParameters.m_Device, &allocateInfo, m_VulkanParameters.m_PresentCommandBuffers.data());

  if (result != VK_SUCCESS) {
    std::cerr << "Could not allocate the present command buffers" << std::endl;
    return false;
  }

  return true;
}

bool VulkanApp::RecordCommandBuffers()
{
  VkCommandBufferBeginInfo commandBufferBeginInfo = {
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, // VkStructureType                          sType;
    nullptr,                                     // const void*                              pNext;
    0,                                           // VkCommandBufferUsageFlags                flags;
    nullptr                                      // const VkCommandBufferInheritanceInfo*    pInheritanceInfo;
  };

  uint32_t commandBufferCount = static_cast<uint32_t>(m_VulkanParameters.m_PresentCommandBuffers.size());
  VkImageSubresourceRange subresourceRange = {
    VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT, // VkImageAspectFlags    aspectMask;
    0,                                                // uint32_t              baseMipLevel;
    1,                                                // uint32_t              levelCount;
    0,                                                // uint32_t              baseArrayLayer;
    1                                                 // uint32_t              layerCount;
  };

  for (uint32_t commandBufferIdx = 0; commandBufferIdx != commandBufferCount; ++commandBufferIdx) {
    VkImageMemoryBarrier fromPresentToDrawBarrier = {
      VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,                    // VkStructureType            sType;
      nullptr,                                                   // const void*                pNext;
      VK_ACCESS_MEMORY_READ_BIT,                                 // VkAccessFlags              srcAccessMask;
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,                      // VkAccessFlags              dstAccessMask;
      VK_IMAGE_LAYOUT_UNDEFINED,                                 // VkImageLayout              oldLayout;
      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,                           // VkImageLayout              newLayout;
      m_VulkanParameters.m_PresentQueueFamilyIdx,                // uint32_t                   srcQueueFamilyIndex;
      m_VulkanParameters.m_PresentQueueFamilyIdx,                // uint32_t                   dstQueueFamilyIndex;
      m_VulkanParameters.m_Swapchain.m_Images[commandBufferIdx], // VkImage                    image;
      subresourceRange                                           // VkImageSubresourceRange    subresourceRange;
    };

    VkImageMemoryBarrier fromDrawToPresentBarrier = {
      VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,                    // VkStructureType            sType;
      nullptr,                                                   // const void*                pNext;
      VkAccessFlagBits::VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,    // VkAccessFlags              srcAccessMask;
      VkAccessFlagBits::VK_ACCESS_MEMORY_READ_BIT,               // VkAccessFlags              dstAccessMask;
      VkImageLayout::VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,            // VkImageLayout              oldLayout;
      VkImageLayout::VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,            // VkImageLayout              newLayout;
      m_VulkanParameters.m_PresentQueueFamilyIdx,                // uint32_t                   srcQueueFamilyIndex;
      m_VulkanParameters.m_PresentQueueFamilyIdx,                // uint32_t                   dstQueueFamilyIndex;
      m_VulkanParameters.m_Swapchain.m_Images[commandBufferIdx], // VkImage                    image;
      subresourceRange                                           // VkImageSubresourceRange    subresourceRange;
    };
    VkClearValue clearValue = { { (85.0f / 255.0f), (87.0f / 255.0f), (112.0f / 255.0f), 0.0f } };

    VkCommandBuffer commandBuffer = m_VulkanParameters.m_PresentCommandBuffers[commandBufferIdx];

    VkRenderPassBeginInfo renderPassBeginInfo = {
      VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,                                // VkStructureType        sType;
      nullptr,                                                                 // const void*            pNext;
      m_VulkanParameters.m_RenderPass,                                         // VkRenderPass           renderPass;
      m_VulkanParameters.m_FramebufferObjects[commandBufferIdx].m_Framebuffer, // VkFramebuffer          framebuffer;
      { { 0, 0 },
        { m_VulkanParameters.m_Swapchain.m_ImageExtent.width,
          m_VulkanParameters.m_Swapchain.m_ImageExtent.height } }, // VkRect2D               renderArea;
      1,                                                           // uint32_t               clearValueCount;
      &clearValue                                                  // const VkClearValue*    pClearValues;
    };
    vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo);
    vkCmdPipelineBarrier(commandBuffer,
                         VkPipelineStageFlagBits::VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VkPipelineStageFlagBits::VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &fromPresentToDrawBarrier);
    vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VkSubpassContents::VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(
      commandBuffer, VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS, m_VulkanParameters.m_Pipeline);

    vkCmdDraw(commandBuffer, 3, 1, 0, 0);

    vkCmdEndRenderPass(commandBuffer);
    vkCmdPipelineBarrier(commandBuffer,
                         VkPipelineStageFlagBits::VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VkPipelineStageFlagBits::VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &fromDrawToPresentBarrier);
    vkEndCommandBuffer(commandBuffer);
  }

  return true;
}

bool VulkanApp::CreateRenderPass()
{
  std::vector<VkAttachmentDescription> attachments = { VkAttachmentDescription{ 0,
                                                                                m_VulkanParameters.m_Swapchain.m_Format,
                                                                                VK_SAMPLE_COUNT_1_BIT,
                                                                                VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                                                VK_ATTACHMENT_STORE_OP_STORE,
                                                                                VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                                                                VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                                                                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                                                                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR } };

  std::vector<VkAttachmentReference> colorAttachments = { VkAttachmentReference{
    0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } };

  std::vector<VkSubpassDescription> subpasses = { VkSubpassDescription{ 0,
                                                                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                                        0,
                                                                        nullptr,
                                                                        static_cast<uint32_t>(colorAttachments.size()),
                                                                        colorAttachments.data(),
                                                                        nullptr,
                                                                        nullptr,
                                                                        0,
                                                                        nullptr } };

  VkRenderPassCreateInfo renderPassCreateInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                                  nullptr,
                                                  0,
                                                  static_cast<uint32_t>(attachments.size()),
                                                  attachments.data(),
                                                  static_cast<uint32_t>(subpasses.size()),
                                                  subpasses.data(),
                                                  0,
                                                  nullptr };

  VkResult const result =
    vkCreateRenderPass(m_VulkanParameters.m_Device, &renderPassCreateInfo, nullptr, &m_VulkanParameters.m_RenderPass);

  if (result != VK_SUCCESS) {
    std::cerr << "Could not create render pass instance: " << result << std::endl;
    return false;
  }
  return true;
}

bool VulkanApp::CreateFramebuffers()
{

  uint32_t const frameBufferObjectCount = static_cast<uint32_t>(m_VulkanParameters.m_Swapchain.m_Images.size());
  m_VulkanParameters.m_FramebufferObjects.resize(frameBufferObjectCount);

  VkResult result;
  for (uint32_t i = 0; i != frameBufferObjectCount; ++i) {
    VkImageViewCreateInfo imageViewCreateInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                                  nullptr,
                                                  0,
                                                  m_VulkanParameters.m_Swapchain.m_Images[i],
                                                  VK_IMAGE_VIEW_TYPE_2D,
                                                  m_VulkanParameters.m_Swapchain.m_Format,
                                                  { VK_COMPONENT_SWIZZLE_IDENTITY,
                                                    VK_COMPONENT_SWIZZLE_IDENTITY,
                                                    VK_COMPONENT_SWIZZLE_IDENTITY,
                                                    VK_COMPONENT_SWIZZLE_IDENTITY },
                                                  { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };

    result = vkCreateImageView(m_VulkanParameters.m_Device,
                               &imageViewCreateInfo,
                               nullptr,
                               &m_VulkanParameters.m_FramebufferObjects[i].m_ImageView);
    if (result != VK_SUCCESS) {
      std::cerr << "Could not create image view for image #" << i << std::endl;
      return false;
    }
  }

  for (uint32_t i = 0; i != frameBufferObjectCount; ++i) {
    VkFramebufferCreateInfo framebufferCreateInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                                                      nullptr,
                                                      0,
                                                      m_VulkanParameters.m_RenderPass,
                                                      1,
                                                      &m_VulkanParameters.m_FramebufferObjects[i].m_ImageView,
                                                      m_VulkanParameters.m_Swapchain.m_ImageExtent.width,
                                                      m_VulkanParameters.m_Swapchain.m_ImageExtent.height,
                                                      1 };

    result = vkCreateFramebuffer(m_VulkanParameters.m_Device,
                                 &framebufferCreateInfo,
                                 nullptr,
                                 &m_VulkanParameters.m_FramebufferObjects[i].m_Framebuffer);
    if (result != VK_SUCCESS) {
      std::cerr << "Could not create framebuffer for image #" << i << std::endl;
      return false;
    }
  }
  return true;
}

bool VulkanApp::CreatePipeline()
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

  VkPipelineVertexInputStateCreateInfo vertexInputState = {
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, // VkStructureType                             sType;
    nullptr,                                                   // const void*                                 pNext;
    0,                                                         // VkPipelineVertexInputStateCreateFlags       flags;
    0,       // uint32_t                                    vertexBindingDescriptionCount;
    nullptr, // const VkVertexInputBindingDescription*      pVertexBindingDescriptions;
    0,       // uint32_t                                    vertexAttributeDescriptionCount;
    nullptr  // const VkVertexInputAttributeDescription*    pVertexAttributeDescriptions;
  };

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {
    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    nullptr,
    0,
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    VK_FALSE
  };

  auto swapchainExtent = GetSwapchainExtent();

  VkViewport viewport = {
    0.0f, 0.0f, static_cast<float>(swapchainExtent.width), static_cast<float>(swapchainExtent.height), 0.0f, 1.0f
  };

  VkRect2D scissorRect = { { 0, 0 }, swapchainExtent };

  VkPipelineViewportStateCreateInfo viewportState = {
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, // VkStructureType                       sType;
    nullptr,                                               // const void*                           pNext;
    0,                                                     // VkPipelineViewportStateCreateFlags    flags;
    1,                                                     // uint32_t                              viewportCount;
    &viewport,                                             // const VkViewport*                     pViewports;
    1,                                                     // uint32_t                              scissorCount;
    &scissorRect                                           // const VkRect2D*                       pScissors;
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
    nullptr,                         // const VkPipelineDynamicStateCreateInfo*          pDynamicState;
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

std::vector<char> VulkanApp::ReadShaderContent(char const* filename)
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

VulkanDeleter<VkShaderModule, PFN_vkDestroyShaderModule> VulkanApp::CreateShaderModule(char const* filename)
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

void VulkanApp::FreeSwapchainAndRenderResources()
{
  if (m_VulkanParameters.m_Pipeline) {
    vkDestroyPipeline(m_VulkanParameters.m_Device, m_VulkanParameters.m_Pipeline, nullptr);
  }

  for (auto& framebufferObject : m_VulkanParameters.m_FramebufferObjects) {
    vkDestroyFramebuffer(m_VulkanParameters.m_Device, framebufferObject.m_Framebuffer, nullptr);
    vkDestroyImageView(m_VulkanParameters.m_Device, framebufferObject.m_ImageView, nullptr);
  }

  m_VulkanParameters.m_FramebufferObjects.clear();

  if (m_VulkanParameters.m_RenderPass) {
    vkDestroyRenderPass(m_VulkanParameters.m_Device, m_VulkanParameters.m_RenderPass, nullptr);
    m_VulkanParameters.m_RenderPass = nullptr;
  }

  if (m_VulkanParameters.m_PresentCommandPool) {
    vkFreeCommandBuffers(m_VulkanParameters.m_Device,
                         m_VulkanParameters.m_PresentCommandPool,
                         static_cast<uint32_t>(m_VulkanParameters.m_PresentCommandBuffers.size()),
                         m_VulkanParameters.m_PresentCommandBuffers.data());
    m_VulkanParameters.m_PresentCommandBuffers.clear();

    vkDestroyCommandPool(m_VulkanParameters.m_Device, m_VulkanParameters.m_PresentCommandPool, nullptr);
    m_VulkanParameters.m_PresentCommandPool = nullptr;
  }

  if (m_VulkanParameters.m_Swapchain.m_Handle) {
    vkDestroySwapchainKHR(m_VulkanParameters.m_Device, m_VulkanParameters.m_Swapchain.m_Handle, nullptr);
    m_VulkanParameters.m_Swapchain.m_Handle = nullptr;
  }
}

VulkanDeleter<VkPipelineLayout, PFN_vkDestroyPipelineLayout> VulkanApp::CreatePipelineLayout()
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


bool VulkanApp::RecreateSwapchainAndRenderResources()
{
  if (vkDeviceWaitIdle(m_VulkanParameters.m_Device) != VK_SUCCESS) {
    std::cerr << "Error while waiting for device idle" << std::endl;
    return false;
  }

  FreeSwapchainAndRenderResources();

  if (!CreateSwapchainAndRenderResources()) {
    return false;
  }

  return true;
}

} // namespace Core
