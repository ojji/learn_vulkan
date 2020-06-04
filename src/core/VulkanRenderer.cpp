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

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

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
  m_Swapchain(Swapchain()),
  m_VsyncEnabled(false),
  m_RenderPass(nullptr),
  m_Pipeline(nullptr),
  m_PipelineLayout(nullptr),
  m_DescriptorSetLayout(nullptr),
  m_DescriptorPool(nullptr),
  m_DescriptorSet(nullptr)
{}

VulkanRenderer::VulkanRenderer(bool vsyncEnabled, uint32_t frameResourcesCount) :
  VulkanRenderer(std::cout, vsyncEnabled, frameResourcesCount)
{}

VulkanRenderer::VulkanRenderer(std::ostream& debugOutput, bool vsyncEnabled, uint32_t frameResourcesCount) :
  m_VulkanParameters(VulkanParameters()),
  m_DebugOutput(debugOutput),
  m_WindowParameters(Os::WindowParameters()),
  m_FrameResourcesCount(frameResourcesCount),
  m_FrameStat(FrameStat()),
  m_GraphicsQueueSubmitCriticalSection(std::mutex()),
  m_TransferQueueSubmitCriticalSection(std::mutex())
{
  m_DebugOutput << std::showbase;
  m_VulkanParameters.m_VsyncEnabled = vsyncEnabled;
}

void VulkanRenderer::Free()
{
  if (m_VulkanParameters.m_Device) {
    m_VulkanParameters.m_Device.waitIdle();

    if (m_VulkanParameters.m_DescriptorPool) {
      m_VulkanParameters.m_Device.destroyDescriptorPool(m_VulkanParameters.m_DescriptorPool);
    }

    if (m_VulkanParameters.m_DescriptorSetLayout) {
      m_VulkanParameters.m_Device.destroyDescriptorSetLayout(m_VulkanParameters.m_DescriptorSetLayout);
    }

    if (m_VulkanParameters.m_QueryPool) {
      m_VulkanParameters.m_Device.destroyQueryPool(m_VulkanParameters.m_QueryPool);
    }

    for (uint32_t i = 0; i != m_FrameResources.size(); ++i) {
      FreeFrameResource(m_FrameResources[i]);
    }

    if (m_VulkanParameters.m_PipelineLayout) {
      m_VulkanParameters.m_Device.destroyPipelineLayout(m_VulkanParameters.m_PipelineLayout);
    }

    if (m_VulkanParameters.m_Pipeline) {
      m_VulkanParameters.m_Device.destroyPipeline(m_VulkanParameters.m_Pipeline);
    }

    if (m_VulkanParameters.m_RenderPass) {
      m_VulkanParameters.m_Device.destroyRenderPass(m_VulkanParameters.m_RenderPass);
      m_VulkanParameters.m_RenderPass = nullptr;
    }

    for (auto& imageView : m_VulkanParameters.m_Swapchain.m_ImageViews) {
      m_VulkanParameters.m_Device.destroyImageView(imageView);
    }

    if (m_VulkanParameters.m_Swapchain.m_Handle) {
      m_VulkanParameters.m_Device.destroySwapchainKHR(m_VulkanParameters.m_Swapchain.m_Handle);
    }

    m_VulkanParameters.m_Device.destroy();
    m_VulkanParameters.m_Device = nullptr;
    m_VulkanParameters.m_GraphicsQueue = nullptr;
    m_VulkanParameters.m_TransferQueue = nullptr;
  }

  if (m_VulkanParameters.m_PresentSurface) {
    m_VulkanParameters.m_Instance.destroySurfaceKHR(m_VulkanParameters.m_PresentSurface);
    m_VulkanParameters.m_PresentSurface = nullptr;
  }

  if (m_VulkanParameters.m_Instance) {
    m_VulkanParameters.m_Instance.destroy();
    m_VulkanParameters.m_Instance = nullptr;
    m_VulkanParameters.m_PhysicalDevice = nullptr;
  }
}

VulkanRenderer::~VulkanRenderer()
{
  Free();
}

bool VulkanRenderer::Initialize(Os::WindowParameters windowParameters)
{
  m_WindowParameters = windowParameters;

  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =
    m_DynamicLoader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
  VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

  uint32_t implementationVersion = GetVulkanImplementationVersion();

#ifdef _DEBUG
  m_DebugOutput << "Vulkan implementation version: " << VK_VERSION_MAJOR(implementationVersion) << "."
                << VK_VERSION_MINOR(implementationVersion) << "." << VK_VERSION_PATCH(implementationVersion)
                << std::endl;
#endif

  std::vector<vk::LayerProperties> layers = GetVulkanLayers();

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
  std::vector<vk::ExtensionProperties> instanceExtensions = GetVulkanInstanceExtensions();

#ifdef _DEBUG
  m_DebugOutput << "\nAvailable instance extensions:" << std::endl;
  for (decltype(instanceExtensions)::size_type idx = 0; idx != instanceExtensions.size(); ++idx) {
    m_DebugOutput << "\t#" << idx << " extensionName: " << instanceExtensions[idx].extensionName
                  << " (specVersion: " << instanceExtensions[idx].specVersion << ")" << std::endl;
  }
#endif

  std::vector<char const*> const requiredExtensions = { VK_KHR_SURFACE_EXTENSION_NAME,
                                                        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#ifdef VK_USE_PLATFORM_WIN32_KHR
                                                        VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#endif
  };

  if (!RequiredInstanceExtensionsAvailable(requiredExtensions)) {
    throw std::runtime_error("Required instance extensions are not available");
  }

  for (decltype(layers)::size_type layerIdx = 0; layerIdx != layers.size(); ++layerIdx) {
    const char* layerName = layers[layerIdx].layerName;
    std::vector<vk::ExtensionProperties> layerExtensions = GetVulkanLayerExtensions(layerName);

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

  if (!CreatePresentationSurface()) {
    std::cerr << "Could not create the presentation surface" << std::endl;
    return false;
  }

  if (!CreateDevice()) {
    return false;
  }

  if (!CreateGraphicsQueue()) {
    return false;
  }

  if (!CreateTransferQueue()) {
    return false;
  }

  if (!CreateSwapchain()) {
    return false;
  }

  if (!CreateDescriptorSetLayout()) {
    return false;
  }

  if (!CreateDescriptorPool()) {
    return false;
  }

  if (!AllocateDescriptorSet()) {
    return false;
  }

  if (!CreateRenderPass()) {
    return false;
  }

  if (!CreatePipeline()) {
    return false;
  }

  if (!CreateQueryPool()) {
    return false;
  }

  return true;
}

uint32_t VulkanRenderer::GetVulkanImplementationVersion() const
{
  return vk::enumerateInstanceVersion();
}

std::vector<vk::LayerProperties> VulkanRenderer::GetVulkanLayers() const
{
  return vk::enumerateInstanceLayerProperties();
}

std::vector<vk::ExtensionProperties> VulkanRenderer::GetVulkanInstanceExtensions() const
{
  return vk::enumerateInstanceExtensionProperties();
}

bool VulkanRenderer::RequiredInstanceExtensionsAvailable(std::vector<char const*> const& requiredExtensions) const
{
  std::vector<vk::ExtensionProperties> instanceExtensions = GetVulkanInstanceExtensions();
  return std::all_of(requiredExtensions.cbegin(), requiredExtensions.cend(), [&](char const* extensionName) {
    for (auto const& instanceExtension : instanceExtensions) {
      if (strcmp(instanceExtension.extensionName, extensionName) == 0) {
        return true;
      }
    }
    return false;
  });
}

std::vector<vk::ExtensionProperties> VulkanRenderer::GetVulkanLayerExtensions(std::string const& layerName) const
{
  return vk::enumerateInstanceExtensionProperties(layerName);
}

bool VulkanRenderer::CreateInstance(std::vector<char const*> const& requiredExtensions)
{
  auto applicationInfo = vk::ApplicationInfo("Learn Vulkan",           // const char* pApplicationName_ = {},
                                             VK_MAKE_VERSION(1, 0, 0), // uint32_t applicationVersion_ = {},
                                             "Learn Vulkan Engine",    // const char* pEngineName_ = {},
                                             VK_MAKE_VERSION(1, 0, 0), // uint32_t engineVersion_ = {},
                                             VK_API_VERSION_1_2        // uint32_t apiVersion_ = {}
  );

  std::vector<char const*> requestedLayers = {
#ifdef _DEBUG
    "VK_LAYER_KHRONOS_validation"
#endif
  };

  auto createInfo =
    vk::InstanceCreateInfo({},               // vk::InstanceCreateFlags flags_ = {}, reserved
                           &applicationInfo, // const vk::ApplicationInfo* pApplicationInfo_ = {},
                           static_cast<uint32_t>(requestedLayers.size()), // uint32_t enabledLayerCount_ = {},
                           requestedLayers.data(), // const char* const* ppEnabledLayerNames_ = {},
                           static_cast<uint32_t>(requiredExtensions.size()), // uint32_t enabledExtensionCount_ = {},
                           requiredExtensions.data() // const char* const* ppEnabledExtensionNames_ = {}
    );

  m_VulkanParameters.m_Instance = vk::createInstance(createInfo);
  VULKAN_HPP_DEFAULT_DISPATCHER.init(m_VulkanParameters.m_Instance);
  return true;
}

bool VulkanRenderer::CreatePresentationSurface()
{
#ifdef VK_USE_PLATFORM_WIN32_KHR
  auto surfaceCreateInfo = vk::Win32SurfaceCreateInfoKHR({}, // vk::Win32SurfaceCreateFlagsKHR flags_ = {}, reserved
                                                         m_WindowParameters.m_Instance, // HINSTANCE hinstance_ = {},
                                                         m_WindowParameters.m_Handle    // HWND hwnd_ = {}
  );

  m_VulkanParameters.m_PresentSurface = m_VulkanParameters.m_Instance.createWin32SurfaceKHR(surfaceCreateInfo);
  return true;
#endif
}

std::vector<vk::ExtensionProperties> VulkanRenderer::GetVulkanDeviceExtensions(vk::PhysicalDevice physicalDevice) const
{
  return physicalDevice.enumerateDeviceExtensionProperties();
}

bool VulkanRenderer::RequiredDeviceExtensionsAvailable(vk::PhysicalDevice physicalDevice,
                                                       std::vector<char const*> const& requiredExtensions) const
{
  std::vector<vk::ExtensionProperties> availableExtensions = GetVulkanDeviceExtensions(physicalDevice);

  return std::all_of(requiredExtensions.cbegin(), requiredExtensions.cend(), [&](char const* extensionName) {
    for (auto const& deviceExtension : availableExtensions) {
      if (strcmp(extensionName, deviceExtension.extensionName) == 0) {
        return true;
      }
    }
    return false;
  });
}

vk::SurfaceCapabilitiesKHR VulkanRenderer::GetDeviceSurfaceCapabilities(vk::PhysicalDevice physicalDevice,
                                                                        vk::SurfaceKHR surface) const
{
  vk::SurfaceCapabilitiesKHR surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);

  // Get surface capabilities
#ifdef _DEBUG
  m_DebugOutput << "\nSurface capabilities:" << std::endl
                << "\tminImageCount: " << surfaceCapabilities.minImageCount << std::endl
                << "\tmaxImageCount: " << surfaceCapabilities.maxImageCount << std::endl
                << "\tcurrentExtent: " << VK_EXPAND_EXTENT2D(surfaceCapabilities.currentExtent) << std::endl
                << "\tminImageExtent: " << VK_EXPAND_EXTENT2D(surfaceCapabilities.minImageExtent) << std::endl
                << "\tmaxImageExtent: " << VK_EXPAND_EXTENT2D(surfaceCapabilities.maxImageExtent) << std::endl
                << "\tmaxImageArrayLayers: " << surfaceCapabilities.maxImageArrayLayers << std::endl
                << "\tsupportedTransforms: " << vk::to_string(surfaceCapabilities.supportedTransforms) << std::endl
                << "\tcurrentTransform: " << vk::to_string(surfaceCapabilities.currentTransform) << std::endl
                << "\tsupportedCompositeAlpha: " << vk::to_string(surfaceCapabilities.supportedCompositeAlpha)
                << std::endl
                << "\tsupportedUsageFlags: " << vk::to_string(surfaceCapabilities.supportedUsageFlags) << std::endl;
#endif

  return surfaceCapabilities;
}

bool VulkanRenderer::CreateDevice()
{
  std::vector<char const*> const requiredDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

  std::vector<vk::PhysicalDevice> physicalDevices = m_VulkanParameters.m_Instance.enumeratePhysicalDevices();

  bool presentQueueFound = false;
  bool transferQueueFound = false;

  for (decltype(physicalDevices)::size_type deviceIdx = 0; deviceIdx != physicalDevices.size(); ++deviceIdx) {
    auto overallProperties = physicalDevices[deviceIdx]
                               .getProperties2<vk::PhysicalDeviceProperties2,
                                               vk::PhysicalDeviceVulkan12Properties,
                                               vk::PhysicalDeviceVulkan11Properties>();

    vk::PhysicalDeviceProperties2 deviceProperties = overallProperties.get<vk::PhysicalDeviceProperties2>();
#ifdef _DEBUG
    m_DebugOutput << "Device #" << deviceIdx << ": " << std::endl
                  << "\tName: " << deviceProperties.properties.deviceName
                  << " (type: " << vk::to_string(deviceProperties.properties.deviceType) << ")" << std::endl
                  << "\tApi version: " << VK_EXPAND_VERSION(deviceProperties.properties.apiVersion) << std::endl
                  << "\tDriver version: " << VK_EXPAND_VERSION(deviceProperties.properties.driverVersion) << std::endl
                  << "\tSome limits: " << std::endl
                  << "\t\tmaxImageDimension2D: " << deviceProperties.properties.limits.maxImageDimension2D << std::endl
                  << "\t\tframebufferColorSampleCounts: "
                  << vk::to_string(deviceProperties.properties.limits.framebufferColorSampleCounts) << std::endl
                  << "\t\tframebufferDepthSampleCounts: "
                  << vk::to_string(deviceProperties.properties.limits.framebufferDepthSampleCounts) << std::endl
                  << "\t\ttimestampPeriod: " << deviceProperties.properties.limits.timestampPeriod << std::endl;
#endif

    vk::PhysicalDeviceFeatures2 deviceFeatures = physicalDevices[deviceIdx].getFeatures2();
#ifdef _DEBUG
    m_DebugOutput << "\nA few device features: " << std::endl
                  << "\tgeometryShader: " << deviceFeatures.features.geometryShader << std::endl
                  << "\ttessellationShader: " << deviceFeatures.features.tessellationShader << std::endl
                  << "\tsamplerAnisotropy: " << deviceFeatures.features.samplerAnisotropy << std::endl
                  << "\tfragmentStoresAndAtomics: " << deviceFeatures.features.fragmentStoresAndAtomics << std::endl
                  << "\talphaToOne: " << deviceFeatures.features.alphaToOne << std::endl;

#endif

    std::vector<vk::ExtensionProperties> deviceExtensions = GetVulkanDeviceExtensions(physicalDevices[deviceIdx]);
#ifdef _DEBUG
    m_DebugOutput << "\nDevice extensions: " << std::endl;
    for (decltype(deviceExtensions)::size_type deviceExtensionIdx = 0; deviceExtensionIdx != deviceExtensions.size();
         ++deviceExtensionIdx) {
      m_DebugOutput << "\t#" << deviceExtensionIdx
                    << " extensionName: " << deviceExtensions[deviceExtensionIdx].extensionName
                    << " (specVersion: " << deviceExtensions[deviceExtensionIdx].specVersion << ")" << std::endl;
    }
#endif

    // Get memory properties
    vk::PhysicalDeviceMemoryProperties memoryProperties = physicalDevices[deviceIdx].getMemoryProperties();
    float bytesInMegaBytes = 1.0f * 1024.0f * 1024.0f;
#ifdef _DEBUG
    m_DebugOutput << "\nMemory properties: " << std::endl;
    for (uint32_t memoryTypeIdx = 0; memoryTypeIdx != memoryProperties.memoryTypeCount; ++memoryTypeIdx) {
      m_DebugOutput << "Memory type " << memoryTypeIdx << std::endl
                    << "\tHeapindex " << memoryProperties.memoryTypes[memoryTypeIdx].heapIndex << std::endl
                    << "\tFlags " << vk::to_string(memoryProperties.memoryTypes[memoryTypeIdx].propertyFlags)
                    << std::endl;
    }

    m_DebugOutput << std::endl;

    for (uint32_t memoryHeapIdx = 0; memoryHeapIdx != memoryProperties.memoryHeapCount; ++memoryHeapIdx) {
      vk::DeviceSize heapSizeInBytes = memoryProperties.memoryHeaps[memoryHeapIdx].size;
      auto heapSizeInMegaBytes = heapSizeInBytes / bytesInMegaBytes;
      m_DebugOutput << "Memory heap " << memoryHeapIdx << std::endl
                    << "\tSize " << heapSizeInBytes << " bytes (" << heapSizeInMegaBytes << " MB)" << std::endl
                    << "\tFlags " << vk::to_string(memoryProperties.memoryHeaps[memoryHeapIdx].flags) << std::endl;
    }
#endif

    // Get device queue family properties
    std::vector<vk::QueueFamilyProperties2> queueFamilyProperties =
      physicalDevices[deviceIdx].getQueueFamilyProperties2();

#ifdef _DEBUG
    m_DebugOutput << "\nQueue family count: " << queueFamilyProperties.size() << std::endl;
    for (decltype(queueFamilyProperties)::size_type queueFamilyIdx = 0; queueFamilyIdx != queueFamilyProperties.size();
         ++queueFamilyIdx) {
      if (queueFamilyIdx != 0) {
        m_DebugOutput << std::endl;
      }
      m_DebugOutput << "\t#" << queueFamilyIdx << " queueFlags: "
                    << vk::to_string(queueFamilyProperties[queueFamilyIdx].queueFamilyProperties.queueFlags)
                    << std::endl
                    << "\t#" << queueFamilyIdx
                    << " queueCount: " << queueFamilyProperties[queueFamilyIdx].queueFamilyProperties.queueCount
                    << std::endl
                    << "\t#" << queueFamilyIdx << " timestampValidBits: "
                    << queueFamilyProperties[queueFamilyIdx].queueFamilyProperties.timestampValidBits << std::endl
                    << "\t#" << queueFamilyIdx << " minImageTransferGranularity: "
                    << VK_EXPAND_EXTENT3D(
                         queueFamilyProperties[queueFamilyIdx].queueFamilyProperties.minImageTransferGranularity)
                    << std::endl;
    }
#endif

    if (RequiredDeviceExtensionsAvailable(physicalDevices[deviceIdx], requiredDeviceExtensions)
        && !m_VulkanParameters.m_PhysicalDevice) {
      for (decltype(queueFamilyProperties)::size_type queueFamilyIdx = 0;
           queueFamilyIdx != queueFamilyProperties.size();
           ++queueFamilyIdx) {
        vk::Bool32 isSurfacePresentationSupported = physicalDevices[deviceIdx].getSurfaceSupportKHR(
          static_cast<uint32_t>(queueFamilyIdx), m_VulkanParameters.m_PresentSurface);

        if ((queueFamilyProperties[queueFamilyIdx].queueFamilyProperties.queueFlags & vk::QueueFlagBits::eGraphics)
            && !presentQueueFound && isSurfacePresentationSupported == VK_TRUE) {
          m_VulkanParameters.m_PhysicalDevice = physicalDevices[deviceIdx];
          m_VulkanParameters.m_GraphicsQueueFamilyIdx = static_cast<VulkanParameters::QueueFamilyIdx>(queueFamilyIdx);
          m_VulkanParameters.m_TimestampPeriod = deviceProperties.properties.limits.timestampPeriod;
          presentQueueFound = true;
        }

        auto currentQueueFlags = queueFamilyProperties[queueFamilyIdx].queueFamilyProperties.queueFlags;
        if ((currentQueueFlags & vk::QueueFlagBits::eTransfer) && !(currentQueueFlags & vk::QueueFlagBits::eGraphics)
            && !(currentQueueFlags & vk::QueueFlagBits::eCompute)) {
          m_VulkanParameters.m_TransferQueueFamilyIdx = static_cast<VulkanParameters::QueueFamilyIdx>(queueFamilyIdx);
          transferQueueFound = true;
        }
      }
    }
  }

  if (!presentQueueFound) {
    throw std::runtime_error("Could not find a suitable device with WSI surface support");
  }

  if (presentQueueFound && !transferQueueFound) {
    m_VulkanParameters.m_TransferQueueFamilyIdx = m_VulkanParameters.m_GraphicsQueueFamilyIdx;
  }

  std::vector<float> const queuePriorities = { 1.0f };

  auto queueCreateInfos = std::vector<vk::DeviceQueueCreateInfo>(
    { vk::DeviceQueueCreateInfo({},                                          // vk::DeviceQueueCreateFlags flags_ = {},
                                m_VulkanParameters.m_GraphicsQueueFamilyIdx, // uint32_t queueFamilyIndex_ = {},
                                static_cast<uint32_t>(queuePriorities.size()), // uint32_t queueCount_ = {},
                                queuePriorities.data()                         // const float* pQueuePriorities_ = {}
                                ),
      vk::DeviceQueueCreateInfo({},                                          // vk::DeviceQueueCreateFlags flags_ = {},
                                m_VulkanParameters.m_TransferQueueFamilyIdx, // uint32_t queueFamilyIndex_ = {},
                                static_cast<uint32_t>(queuePriorities.size()), // uint32_t queueCount_ = {},
                                queuePriorities.data()                         // const float* pQueuePriorities_ = {}
                                ) });

  auto deviceCreateInfo = vk::DeviceCreateInfo(
    {},                                                     // vk::DeviceCreateFlags flags_ = {}, reserved
    static_cast<uint32_t>(queueCreateInfos.size()),         // uint32_t queueCreateInfoCount_ = {},
    queueCreateInfos.data(),                                // const vk::DeviceQueueCreateInfo* pQueueCreateInfos_ = {},
    0,                                                      // uint32_t enabledLayerCount_ = {},
    nullptr,                                                // const char* const* ppEnabledLayerNames_ = {},
    static_cast<uint32_t>(requiredDeviceExtensions.size()), // uint32_t enabledExtensionCount_ = {},
    requiredDeviceExtensions.data(),                        // const char* const* ppEnabledExtensionNames_ = {},
    nullptr                                                 // const vk::PhysicalDeviceFeatures* pEnabledFeatures_ = {}
  );

  m_VulkanParameters.m_Device = m_VulkanParameters.m_PhysicalDevice.createDevice(deviceCreateInfo);
  VULKAN_HPP_DEFAULT_DISPATCHER.init(m_VulkanParameters.m_Device);

  return true;
}


std::vector<vk::SurfaceFormatKHR> VulkanRenderer::GetSupportedSurfaceFormats(vk::PhysicalDevice physicalDevice,
                                                                             vk::SurfaceKHR surface) const
{
  return physicalDevice.getSurfaceFormatsKHR(surface);
}

std::vector<vk::PresentModeKHR> VulkanRenderer::GetSupportedPresentationModes(vk::PhysicalDevice physicalDevice,
                                                                              vk::SurfaceKHR surface) const
{
  return physicalDevice.getSurfacePresentModesKHR(surface);
}

vk::PresentModeKHR VulkanRenderer::GetSwapchainPresentMode(
  std::vector<vk::PresentModeKHR> const& supportedPresentationModes) const
{
  auto presentModeSupported = [&](vk::PresentModeKHR mode) {
    return std::find(supportedPresentationModes.cbegin(), supportedPresentationModes.cend(), mode)
           != supportedPresentationModes.cend();
  };

  if (m_VulkanParameters.m_VsyncEnabled && presentModeSupported(vk::PresentModeKHR::eMailbox)) {
    return vk::PresentModeKHR::eMailbox;
  }

  if (m_VulkanParameters.m_VsyncEnabled && presentModeSupported(vk::PresentModeKHR::eFifo)) {
    return vk::PresentModeKHR::eFifo;
  }

  if (!m_VulkanParameters.m_VsyncEnabled && presentModeSupported(vk::PresentModeKHR::eImmediate)) {
    return vk::PresentModeKHR::eImmediate;
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

vk::SurfaceFormatKHR VulkanRenderer::GetSwapchainFormat(
  std::vector<vk::SurfaceFormatKHR> const& supportedSurfaceFormats) const
{
  vk::SurfaceFormatKHR desiredSurfaceFormat;
  bool foundBGRA = false;
  bool foundRGBA = false;
  for (uint32_t idx = 0; idx != supportedSurfaceFormats.size(); ++idx) {
    if (supportedSurfaceFormats[idx].format == vk::Format::eB8G8R8A8Unorm
        && supportedSurfaceFormats[idx].colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
      desiredSurfaceFormat = supportedSurfaceFormats[idx];
      foundBGRA = true;
    }
    if (supportedSurfaceFormats[idx].format == vk::Format::eR8G8B8A8Unorm
        && supportedSurfaceFormats[idx].colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear && !foundBGRA) {
      desiredSurfaceFormat = supportedSurfaceFormats[idx];
      foundRGBA = true;
    }
  }

  if (foundBGRA || foundRGBA) {
    return desiredSurfaceFormat;
  }

  return supportedSurfaceFormats[0];
}

vk::Extent2D VulkanRenderer::GetSwapchainExtent() const
{
  RECT currentRect;
  GetClientRect(m_WindowParameters.m_Handle, &currentRect);
  return vk::Extent2D(static_cast<uint32_t>(currentRect.right - currentRect.left),
                      static_cast<uint32_t>(currentRect.bottom - currentRect.top));
}

vk::ImageUsageFlags VulkanRenderer::GetSwapchainUsageFlags() const
{
  if ((m_VulkanParameters.m_SurfaceCapabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferDst)) {
    return vk::ImageUsageFlags({ vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst });
  }

  throw std::runtime_error("Could not create an usage flag bitmask with VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | "
                           "VK_IMAGE_USAGE_TRANSFER_DST_BIT");
}

vk::SurfaceTransformFlagBitsKHR VulkanRenderer::GetSwapchainTransform() const
{
  if ((m_VulkanParameters.m_SurfaceCapabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity)) {
    return vk::SurfaceTransformFlagBitsKHR::eIdentity;
  }
  return m_VulkanParameters.m_SurfaceCapabilities.currentTransform;
}

bool VulkanRenderer::CreateSwapchain()
{
  m_CanRender = false;
  m_VulkanParameters.m_SurfaceCapabilities =
    GetDeviceSurfaceCapabilities(m_VulkanParameters.m_PhysicalDevice, m_VulkanParameters.m_PresentSurface);

  std::vector<vk::SurfaceFormatKHR> supportedSurfaceFormats =
    GetSupportedSurfaceFormats(m_VulkanParameters.m_PhysicalDevice, m_VulkanParameters.m_PresentSurface);

#ifdef _DEBUG
  m_DebugOutput << "\nSupported surface format pairs: " << std::endl;
  for (decltype(supportedSurfaceFormats)::size_type idx = 0; idx != supportedSurfaceFormats.size(); ++idx) {
    if (idx != 0) {
      m_DebugOutput << std::endl;
    }
    m_DebugOutput << "\t#" << idx << " colorSpace: " << vk::to_string(supportedSurfaceFormats[idx].colorSpace)
                  << std::endl
                  << "\t#" << idx << " format: " << vk::to_string(supportedSurfaceFormats[idx].format) << std::endl;
  }
#endif

  std::vector<vk::PresentModeKHR> supportedPresentationModes =
    GetSupportedPresentationModes(m_VulkanParameters.m_PhysicalDevice, m_VulkanParameters.m_PresentSurface);

#ifdef _DEBUG
  m_DebugOutput << "\nSupported presentation modes: " << std::endl;
  for (decltype(supportedPresentationModes)::size_type idx = 0; idx != supportedPresentationModes.size(); ++idx) {
    m_DebugOutput << "\t#" << idx << ": " << vk::to_string(supportedPresentationModes[idx]) << std::endl;
  }
#endif

  uint32_t const desiredImageCount = GetSwapchainImageCount();
  vk::SurfaceFormatKHR const desiredImageFormat = GetSwapchainFormat(supportedSurfaceFormats);
  vk::Extent2D const desiredImageExtent = GetSwapchainExtent();
  vk::ImageUsageFlags const desiredSwapchainUsageFlags = GetSwapchainUsageFlags();
  vk::SurfaceTransformFlagBitsKHR const desiredSwapchainTransform = GetSwapchainTransform();
  vk::PresentModeKHR const desiredPresentationMode = GetSwapchainPresentMode(supportedPresentationModes);

#ifdef _DEBUG
  m_DebugOutput << "\nSwapchain creation setup:" << std::endl
                << "\tImage count: " << desiredImageCount << std::endl
                << "\tImage format: " << vk::to_string(desiredImageFormat.format) << std::endl
                << "\tColor space: " << vk::to_string(desiredImageFormat.colorSpace) << std::endl
                << "\tImage extent: " << VK_EXPAND_EXTENT2D(desiredImageExtent) << std::endl
                << "\tUsage flags: " << vk::to_string(desiredSwapchainUsageFlags) << std::endl
                << "\tSurface transform: " << vk::to_string(desiredSwapchainTransform) << std::endl
                << "\tPresentation mode: " << vk::to_string(desiredPresentationMode) << std::endl;
#endif

  // NOTE: There is a race condition here. If the OS changes the window size between the calls to
  // vkGetPhysicalDeviceSurfaceCapabilitiesKHR() and to vkCreateSwapchainKHR() the currentExtent will be invalid.
  // Either disable rendering between WM_ENTERSIZEMOVE and WM_EXITSIZEMOVE or live with this condition - not sure if
  // there is anything else we could do.
  vk::SurfaceCapabilitiesKHR caps =
    m_VulkanParameters.m_PhysicalDevice.getSurfaceCapabilitiesKHR(m_VulkanParameters.m_PresentSurface);
  uint32_t width = std::clamp(desiredImageExtent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
  uint32_t height = std::clamp(desiredImageExtent.height, caps.minImageExtent.height, caps.maxImageExtent.height);

  vk::SwapchainCreateInfoKHR swapchainCreateInfo = vk::SwapchainCreateInfoKHR(
    {},                                     // vk::SwapchainCreateFlagsKHR flags_ = {},
    m_VulkanParameters.m_PresentSurface,    // vk::SurfaceKHR surface_ = {},
    desiredImageCount,                      // uint32_t minImageCount_ = {},
    desiredImageFormat.format,              // vk::Format imageFormat_ = vk::Format::eUndefined,
    desiredImageFormat.colorSpace,          // vk::ColorSpaceKHR imageColorSpace_ = vk::ColorSpaceKHR::eSrgbNonlinear,
    vk::Extent2D(width, height),            // vk::Extent2D imageExtent_ = {},
    1,                                      // uint32_t imageArrayLayers_ = {},
    desiredSwapchainUsageFlags,             // vk::ImageUsageFlags imageUsage_ = {},
    vk::SharingMode::eExclusive,            // vk::SharingMode imageSharingMode_ = vk::SharingMode::eExclusive,
    0,                                      // uint32_t queueFamilyIndexCount_ = {},
    nullptr,                                // const uint32_t* pQueueFamilyIndices_ = {},
    desiredSwapchainTransform,              // vk::SurfaceTransformFlagBitsKHR preTransform_ =
                                            // vk::SurfaceTransformFlagBitsKHR::eIdentity,
    vk::CompositeAlphaFlagBitsKHR::eOpaque, // vk::CompositeAlphaFlagBitsKHR compositeAlpha_ =
                                            // vk::CompositeAlphaFlagBitsKHR::eOpaque,
    desiredPresentationMode,                // vk::PresentModeKHR presentMode_ = vk::PresentModeKHR::eImmediate,
    VK_TRUE,                                // vk::Bool32 clipped_ = {},
    m_VulkanParameters.m_Swapchain.m_Handle // vk::SwapchainKHR oldSwapchain_ = {}
  );

  m_VulkanParameters.m_Swapchain.m_Handle = m_VulkanParameters.m_Device.createSwapchainKHR(swapchainCreateInfo);
  m_VulkanParameters.m_Swapchain.m_Images =
    m_VulkanParameters.m_Device.getSwapchainImagesKHR(m_VulkanParameters.m_Swapchain.m_Handle);
  m_VulkanParameters.m_Swapchain.m_Format = desiredImageFormat.format;
  m_VulkanParameters.m_Swapchain.m_ImageExtent = vk::Extent2D(width, height);

  return CreateSwapchainImageViews();
}

bool VulkanRenderer::CreateSwapchainImageViews()
{
  uint32_t swapchainImagesCount = static_cast<uint32_t>(m_VulkanParameters.m_Swapchain.m_Images.size());
  m_VulkanParameters.m_Swapchain.m_ImageViews.clear();
  m_VulkanParameters.m_Swapchain.m_ImageViews.resize(swapchainImagesCount);

  for (uint32_t i = 0; i != swapchainImagesCount; ++i) {
    auto imageViewCreateInfo = vk::ImageViewCreateInfo(
      {},                                         // vk::ImageViewCreateFlags flags_ = {},
      m_VulkanParameters.m_Swapchain.m_Images[i], // vk::Image image_ = {},
      vk::ImageViewType::e2D,                     // vk::ImageViewType viewType_ = vk::ImageViewType::e1D,
      m_VulkanParameters.m_Swapchain.m_Format,    // vk::Format format_ = vk::Format::eUndefined,
      vk::ComponentMapping(
        vk::ComponentSwizzle::eIdentity, // vk::ComponentSwizzle r_ = vk::ComponentSwizzle::eIdentity,
        vk::ComponentSwizzle::eIdentity, // vk::ComponentSwizzle g_ = vk::ComponentSwizzle::eIdentity,
        vk::ComponentSwizzle::eIdentity, // vk::ComponentSwizzle b_ = vk::ComponentSwizzle::eIdentity,
        vk::ComponentSwizzle::eIdentity  // vk::ComponentSwizzle a_ = vk::ComponentSwizzle::eIdentity
        ),                               // vk::ComponentMapping components_ = {},
      vk::ImageSubresourceRange({ vk::ImageAspectFlagBits::eColor }, // vk::ImageAspectFlags aspectMask_ = {},
                                0,                                   // uint32_t baseMipLevel_ = {},
                                1,                                   // uint32_t levelCount_ = {},
                                0,                                   // uint32_t baseArrayLayer_ = {},
                                1                                    // uint32_t layerCount_ = {}
                                )                                    // vk::ImageSubresourceRange subresourceRange_ = {}
    );

    m_VulkanParameters.m_Swapchain.m_ImageViews[i] = m_VulkanParameters.m_Device.createImageView(imageViewCreateInfo);
  }
  m_CanRender = true;
  return true;
}

bool VulkanRenderer::CreateGraphicsQueue()
{
  m_VulkanParameters.m_GraphicsQueue =
    m_VulkanParameters.m_Device.getQueue(m_VulkanParameters.m_GraphicsQueueFamilyIdx, 0);
  return true;
}

bool VulkanRenderer::CreateTransferQueue()
{
  m_VulkanParameters.m_TransferQueue =
    m_VulkanParameters.m_Device.getQueue(m_VulkanParameters.m_TransferQueueFamilyIdx, 0);
  return true;
}

vk::CommandPool VulkanRenderer::CreateGraphicsCommandPool()
{
  auto commandPoolCreateInfo = vk::CommandPoolCreateInfo(
    { vk::CommandPoolCreateFlagBits::eResetCommandBuffer
      | vk::CommandPoolCreateFlagBits::eTransient }, // vk::CommandPoolCreateFlags flags_ = {},
    m_VulkanParameters.m_GraphicsQueueFamilyIdx      // uint32_t queueFamilyIndex_ = {}
  );

  return m_VulkanParameters.m_Device.createCommandPool(commandPoolCreateInfo);
}

vk::CommandPool VulkanRenderer::CreateTransferCommandPool()
{
  auto commandPoolCreateInfo = vk::CommandPoolCreateInfo(
    { vk::CommandPoolCreateFlagBits::eResetCommandBuffer
      | vk::CommandPoolCreateFlagBits::eTransient }, // vk::CommandPoolCreateFlags flags_ = {},
    m_VulkanParameters.m_TransferQueueFamilyIdx      // uint32_t queueFamilyIndex_ = {}
  );

  return m_VulkanParameters.m_Device.createCommandPool(commandPoolCreateInfo);
}

vk::CommandBuffer VulkanRenderer::AllocateCommandBuffer(vk::CommandPool commandPool)
{
  auto allocateInfo = vk::CommandBufferAllocateInfo(
    commandPool,                      // vk::CommandPool commandPool_ = {},
    vk::CommandBufferLevel::ePrimary, // vk::CommandBufferLevel level_ = vk::CommandBufferLevel::ePrimary,
    1                                 // uint32_t commandBufferCount_ = {}
  );

  return m_VulkanParameters.m_Device.allocateCommandBuffers(allocateInfo)[0];
}

void VulkanRenderer::FreeFrameResource(FrameResource& frameResource)
{
  if (frameResource.m_Fence) {
    m_VulkanParameters.m_Device.destroyFence(frameResource.m_Fence);
  }
  if (frameResource.m_PresentToDrawSemaphore) {
    m_VulkanParameters.m_Device.destroySemaphore(frameResource.m_PresentToDrawSemaphore);
  }
  if (frameResource.m_DrawToPresentSemaphore) {
    m_VulkanParameters.m_Device.destroySemaphore(frameResource.m_DrawToPresentSemaphore);
  }
  if (frameResource.m_Framebuffer) {
    m_VulkanParameters.m_Device.destroyFramebuffer(frameResource.m_Framebuffer);
  }
  if (frameResource.m_QueryPool) {
    m_VulkanParameters.m_Device.destroyQueryPool(frameResource.m_QueryPool);
  }
}

bool VulkanRenderer::CreateQueryPool()
{
  auto queryPoolCreateInfo =
    vk::QueryPoolCreateInfo({},                        // vk::QueryPoolCreateFlags flags_ = {}, reserved
                            vk::QueryType::eTimestamp, // vk::QueryType queryType_ = vk::QueryType::eOcclusion,
                            2,                         // uint32_t queryCount_ = {},
                            {}                         // vk::QueryPipelineStatisticFlags pipelineStatistics_ = {}
    );

  m_VulkanParameters.m_QueryPool = m_VulkanParameters.m_Device.createQueryPool(queryPoolCreateInfo);
  return true;
}

bool VulkanRenderer::CreateSemaphores(FrameResource& frameResource)
{
  auto semaphoreCreateInfo = vk::SemaphoreCreateInfo({});
  frameResource.m_PresentToDrawSemaphore = m_VulkanParameters.m_Device.createSemaphore(semaphoreCreateInfo);
  frameResource.m_DrawToPresentSemaphore = m_VulkanParameters.m_Device.createSemaphore(semaphoreCreateInfo);
  return true;
}

bool VulkanRenderer::CreateFence(FrameResource& frameResource)
{
  auto fenceCreateInfo = vk::FenceCreateInfo({ vk::FenceCreateFlagBits::eSignaled });
  frameResource.m_Fence = m_VulkanParameters.m_Device.createFence(fenceCreateInfo);
  return true;
}

void VulkanRenderer::InitializeFrameResources()
{
  m_FrameResources.clear();
  m_FrameResources.resize(m_FrameResourcesCount);

  for (uint32_t i = 0; i != m_FrameResources.size(); ++i) {
    m_FrameResources[i].m_FrameIdx = i;
    if (!CreateSemaphores(m_FrameResources[i])) {
      throw std::runtime_error("Could not create semaphores for render resource #" + i);
    }

    if (!CreateFence(m_FrameResources[i])) {
      throw std::runtime_error("Could not create fence for render resource #" + i);
    }

    auto queryPoolCreateInfo =
      vk::QueryPoolCreateInfo({},                        // vk::QueryPoolCreateFlags flags_ = {}, reserved
                              vk::QueryType::eTimestamp, // vk::QueryType queryType_ = vk::QueryType::eOcclusion,
                              2,                         // uint32_t queryCount_ = {},
                              {}                         // vk::QueryPipelineStatisticFlags pipelineStatistics_ = {}
      );

    m_FrameResources[i].m_QueryPool = m_VulkanParameters.m_Device.createQueryPool(queryPoolCreateInfo);
  }
}

std::tuple<vk::Result, FrameResource> VulkanRenderer::AcquireNextFrameResources()
{
  uint32_t currentResourceIdx = (InterlockedIncrement(&m_CurrentResourceIdx) - 1) % m_FrameResourcesCount;
  auto result = m_VulkanParameters.m_Device.waitForFences(
    m_FrameResources[currentResourceIdx].m_Fence, VK_FALSE, std::numeric_limits<uint64_t>::max());
  if (result != vk::Result::eSuccess) {
    std::cerr << "Wait on fence timed out" << std::endl;
    return { result, FrameResource() };
  }

  vk::ResultValue acquireResult =
    m_VulkanParameters.m_Device.acquireNextImageKHR(m_VulkanParameters.m_Swapchain.m_Handle,
                                                    std::numeric_limits<uint64_t>::max(),
                                                    m_FrameResources[currentResourceIdx].m_PresentToDrawSemaphore,
                                                    nullptr);

  if (acquireResult.result != vk::Result::eSuccess) {
    return { result, FrameResource() };
  }

  m_FrameResources[currentResourceIdx].m_SwapchainImage.m_ImageIdx = acquireResult.value;
  m_FrameResources[currentResourceIdx].m_SwapchainImage.m_ImageView =
    m_VulkanParameters.m_Swapchain.m_ImageViews[acquireResult.value];
  m_FrameResources[currentResourceIdx].m_SwapchainImage.m_ImageWidth =
    m_VulkanParameters.m_Swapchain.m_ImageExtent.width;
  m_FrameResources[currentResourceIdx].m_SwapchainImage.m_ImageHeight =
    m_VulkanParameters.m_Swapchain.m_ImageExtent.height;

  if (m_FrameResources[currentResourceIdx].m_Framebuffer) {
    m_VulkanParameters.m_Device.destroyFramebuffer(m_FrameResources[currentResourceIdx].m_Framebuffer);
    m_FrameResources[currentResourceIdx].m_Framebuffer = nullptr;
  }

  auto framebufferCreateInfo = vk::FramebufferCreateInfo(
    {},                                                                // vk::FramebufferCreateFlags flags_ = {},
    m_VulkanParameters.m_RenderPass,                                   // vk::RenderPass renderPass_ = {},
    1,                                                                 // uint32_t attachmentCount_ = {},
    &m_VulkanParameters.m_Swapchain.m_ImageViews[acquireResult.value], // const vk::ImageView* pAttachments_ = {},
    m_VulkanParameters.m_Swapchain.m_ImageExtent.width,                // uint32_t width_ = {},
    m_VulkanParameters.m_Swapchain.m_ImageExtent.height,               // uint32_t height_ = {},
    1                                                                  // uint32_t layers_ = {}
  );

  m_FrameResources[currentResourceIdx].m_Framebuffer =
    m_VulkanParameters.m_Device.createFramebuffer(framebufferCreateInfo);

  return { acquireResult.result, m_FrameResources[currentResourceIdx] };
}

vk::Result VulkanRenderer::PresentFrame(FrameResource& frameResources)
{
  auto presentInfo =
    vk::PresentInfoKHR(1,                                           // uint32_t waitSemaphoreCount_ = {},
                       &frameResources.m_DrawToPresentSemaphore,    // const vk::Semaphore* pWaitSemaphores_ = {},
                       1,                                           // uint32_t swapchainCount_ = {},
                       &m_VulkanParameters.m_Swapchain.m_Handle,    // const vk::SwapchainKHR* pSwapchains_ = {},
                       &frameResources.m_SwapchainImage.m_ImageIdx, // const uint32_t* pImageIndices_ = {},
                       nullptr                                      // vk::Result* pResults_ = {}
    );

  auto presentResult = m_VulkanParameters.m_GraphicsQueue.presentKHR(presentInfo);

  // Frame time data
  frameResources.m_FrameStat.m_BeginFrameTimestamp = 0;
  frameResources.m_FrameStat.m_EndFrameTimestamp = 0;
  m_VulkanParameters.m_Device.getQueryPoolResults(frameResources.m_QueryPool,
                                                  0,
                                                  2,
                                                  2 * sizeof(uint64_t),
                                                  &frameResources.m_FrameStat,
                                                  sizeof(uint64_t),
                                                  { vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait });
  return presentResult;
}

double VulkanRenderer::GetFrameTimeInMs(FrameStat const& frameStat)
{
  double frameTimeInMs = static_cast<double>(frameStat.m_EndFrameTimestamp - frameStat.m_BeginFrameTimestamp)
                         * static_cast<double>(m_VulkanParameters.m_TimestampPeriod) / 1'000'000.0;
  return frameTimeInMs;
}

void VulkanRenderer::BeginFrame(FrameResource& frameResources, vk::CommandBuffer commandBuffer)
{
  commandBuffer.begin(vk::CommandBufferBeginInfo({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit }, nullptr));
  commandBuffer.resetQueryPool(frameResources.m_QueryPool, 0, 2);
  commandBuffer.writeTimestamp({ vk::PipelineStageFlagBits::eBottomOfPipe }, frameResources.m_QueryPool, 0);

  auto subresourceRange =
    vk::ImageSubresourceRange({ vk::ImageAspectFlagBits::eColor }, // vk::ImageAspectFlags aspectMask_ = {},
                              0,                                   // uint32_t baseMipLevel_ = {},
                              1,                                   // uint32_t levelCount_ = {},
                              0,                                   // uint32_t baseArrayLayer_ = {},
                              1                                    // uint32_t layerCount_ = {}
    );

  auto fromPresentToDrawBarrier = vk::ImageMemoryBarrier(
    { vk::AccessFlagBits::eMemoryRead },           // vk::AccessFlags srcAccessMask_ = {},
    { vk::AccessFlagBits::eColorAttachmentWrite }, // vk::AccessFlags dstAccessMask_ = {},
    vk::ImageLayout::eUndefined,                   // vk::ImageLayout oldLayout_ = vk::ImageLayout::eUndefined,
    vk::ImageLayout::ePresentSrcKHR,               // vk::ImageLayout newLayout_ = vk::ImageLayout::eUndefined,
    m_VulkanParameters.m_GraphicsQueueFamilyIdx,   // uint32_t srcQueueFamilyIndex_ = {},
    m_VulkanParameters.m_GraphicsQueueFamilyIdx,   // uint32_t dstQueueFamilyIndex_ = {},
    m_VulkanParameters.m_Swapchain.m_Images[frameResources.m_SwapchainImage.m_ImageIdx], // vk::Image image_ = {},
    subresourceRange // vk::ImageSubresourceRange subresourceRange_ = {}
  );

  commandBuffer.pipelineBarrier({ vk::PipelineStageFlagBits::eColorAttachmentOutput },
                                { vk::PipelineStageFlagBits::eColorAttachmentOutput },
                                {},
                                nullptr,
                                nullptr,
                                fromPresentToDrawBarrier);
}

void VulkanRenderer::EndFrame(FrameResource& frameResources, vk::CommandBuffer commandBuffer)
{
  auto subresourceRange =
    vk::ImageSubresourceRange({ vk::ImageAspectFlagBits::eColor }, // vk::ImageAspectFlags aspectMask_ = {},
                              0,                                   // uint32_t baseMipLevel_ = {},
                              1,                                   // uint32_t levelCount_ = {},
                              0,                                   // uint32_t baseArrayLayer_ = {},
                              1                                    // uint32_t layerCount_ = {}
    );

  auto fromDrawToPresentBarrier = vk::ImageMemoryBarrier(
    { vk::AccessFlagBits::eColorAttachmentWrite }, // vk::AccessFlags srcAccessMask_ = {},
    { vk::AccessFlagBits::eMemoryRead },           // vk::AccessFlags dstAccessMask_ = {},
    vk::ImageLayout::ePresentSrcKHR,               // vk::ImageLayout oldLayout_ = vk::ImageLayout::eUndefined,
    vk::ImageLayout::ePresentSrcKHR,               // vk::ImageLayout newLayout_ = vk::ImageLayout::eUndefined,
    m_VulkanParameters.m_GraphicsQueueFamilyIdx,   // uint32_t srcQueueFamilyIndex_ = {},
    m_VulkanParameters.m_GraphicsQueueFamilyIdx,   // uint32_t dstQueueFamilyIndex_ = {},
    m_VulkanParameters.m_Swapchain.m_Images[frameResources.m_SwapchainImage.m_ImageIdx], // vk::Image image_ = {},
    subresourceRange // vk::ImageSubresourceRange subresourceRange_ = {}
  );

  commandBuffer.pipelineBarrier({ vk::PipelineStageFlagBits::eColorAttachmentOutput },
                                { vk::PipelineStageFlagBits::eBottomOfPipe },
                                {},
                                nullptr,
                                nullptr,
                                fromDrawToPresentBarrier);

  commandBuffer.writeTimestamp({ vk::PipelineStageFlagBits::eBottomOfPipe }, frameResources.m_QueryPool, 1);
  commandBuffer.end();
}

bool VulkanRenderer::CreateDescriptorSetLayout()
{
  auto bindings = std::vector<vk::DescriptorSetLayoutBinding>({ vk::DescriptorSetLayoutBinding(
    0,                                         // uint32_t binding_ = {},
    vk::DescriptorType::eCombinedImageSampler, // vk::DescriptorType descriptorType_ = vk::DescriptorType::eSampler,
    1,                                         // uint32_t descriptorCount_ = {},
    { vk::ShaderStageFlagBits::eFragment },    // vk::ShaderStageFlags stageFlags_ = {},
    nullptr                                    // const vk::Sampler* pImmutableSamplers_ = {}
    ) });

  auto descriptorSetLayoutCreateInfo =
    vk::DescriptorSetLayoutCreateInfo({}, // vk::DescriptorSetLayoutCreateFlags flags_ = {},
                                      static_cast<uint32_t>(bindings.size()), // uint32_t bindingCount_ = {},
                                      bindings.data() // const vk::DescriptorSetLayoutBinding* pBindings_ = {}
    );

  m_VulkanParameters.m_DescriptorSetLayout =
    m_VulkanParameters.m_Device.createDescriptorSetLayout(descriptorSetLayoutCreateInfo);
  return true;
}

bool VulkanRenderer::CreateDescriptorPool()
{
  auto poolSizes = std::vector<vk::DescriptorPoolSize>({ vk::DescriptorPoolSize(
    vk::DescriptorType::eCombinedImageSampler, // vk::DescriptorType type_ = vk::DescriptorType::eSampler,
    1                                          // uint32_t descriptorCount_ = {}
    ) });

  auto descriptorPoolCreateInfo =
    vk::DescriptorPoolCreateInfo({},                                      // vk::DescriptorPoolCreateFlags flags_ = {},
                                 1,                                       // uint32_t maxSets_ = {},
                                 static_cast<uint32_t>(poolSizes.size()), // uint32_t poolSizeCount_ = {},
                                 poolSizes.data() // const vk::DescriptorPoolSize* pPoolSizes_ = {}
    );

  m_VulkanParameters.m_DescriptorPool = m_VulkanParameters.m_Device.createDescriptorPool(descriptorPoolCreateInfo);
  return true;
}

bool VulkanRenderer::AllocateDescriptorSet()
{
  auto descriptorSetAllocateInfo = vk::DescriptorSetAllocateInfo(
    m_VulkanParameters.m_DescriptorPool,      // vk::DescriptorPool descriptorPool_ = {},
    1,                                        // uint32_t descriptorSetCount_ = {},
    &m_VulkanParameters.m_DescriptorSetLayout // const vk::DescriptorSetLayout* pSetLayouts_ = {}
  );

  std::vector<vk::DescriptorSet> descriptorSets =
    m_VulkanParameters.m_Device.allocateDescriptorSets(descriptorSetAllocateInfo);
  m_VulkanParameters.m_DescriptorSet = descriptorSets[0];
  return true;
}

bool VulkanRenderer::CreateRenderPass()
{
  auto attachments = std::vector<vk::AttachmentDescription>({ vk::AttachmentDescription(
    {},                                      // vk::AttachmentDescriptionFlags flags_ = {},
    m_VulkanParameters.m_Swapchain.m_Format, // vk::Format format_ = vk::Format::eUndefined,
    vk::SampleCountFlagBits::e1,             // vk::SampleCountFlagBits samples_ = vk::SampleCountFlagBits::e1,
    vk::AttachmentLoadOp::eClear,            // vk::AttachmentLoadOp loadOp_ = vk::AttachmentLoadOp::eLoad,
    vk::AttachmentStoreOp::eStore,           // vk::AttachmentStoreOp storeOp_ = vk::AttachmentStoreOp::eStore,
    vk::AttachmentLoadOp::eDontCare,         // vk::AttachmentLoadOp stencilLoadOp_ = vk::AttachmentLoadOp::eLoad,
    vk::AttachmentStoreOp::eDontCare,        // vk::AttachmentStoreOp stencilStoreOp_ = vk::AttachmentStoreOp::eStore,
    vk::ImageLayout::ePresentSrcKHR,         // vk::ImageLayout initialLayout_ = vk::ImageLayout::eUndefined,
    vk::ImageLayout::ePresentSrcKHR          // vk::ImageLayout finalLayout_ = vk::ImageLayout::eUndefined
    ) });

  auto colorAttachments = std::vector<vk::AttachmentReference>({ vk::AttachmentReference(
    0,                                       // uint32_t attachment_ = {},
    vk::ImageLayout::eColorAttachmentOptimal // vk::ImageLayout layout_ = vk::ImageLayout::eUndefined
    ) });

  auto subpasses = std::vector<vk::SubpassDescription>({ vk::SubpassDescription(
    {},                               // vk::SubpassDescriptionFlags flags_ = {},
    vk::PipelineBindPoint::eGraphics, // vk::PipelineBindPoint pipelineBindPoint_ = vk::PipelineBindPoint::eGraphics,
    0,                                // uint32_t inputAttachmentCount_ = {},
    nullptr,                          // const vk::AttachmentReference* pInputAttachments_ = {},
    static_cast<uint32_t>(colorAttachments.size()), // uint32_t colorAttachmentCount_ = {},
    colorAttachments.data(),                        // const vk::AttachmentReference* pColorAttachments_ = {},
    nullptr,                                        // const vk::AttachmentReference* pResolveAttachments_ = {},
    nullptr,                                        // const vk::AttachmentReference* pDepthStencilAttachment_ = {},
    0,                                              // uint32_t preserveAttachmentCount_ = {},
    nullptr                                         // const uint32_t* pPreserveAttachments_ = {}
    ) });

  auto renderPassCreateInfo =
    vk::RenderPassCreateInfo({},                                        // vk::RenderPassCreateFlags flags_ = {},
                             static_cast<uint32_t>(attachments.size()), // uint32_t attachmentCount_ = {},
                             attachments.data(), // const vk::AttachmentDescription* pAttachments_ = {},
                             static_cast<uint32_t>(subpasses.size()), // uint32_t subpassCount_ = {},
                             subpasses.data(),                        // const vk::SubpassDescription* pSubpasses_ = {},
                             0,                                       // uint32_t dependencyCount_ = {},
                             nullptr // const vk::SubpassDependency* pDependencies_ = {}
    );

  m_VulkanParameters.m_RenderPass = m_VulkanParameters.m_Device.createRenderPass(renderPassCreateInfo);
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

  auto shaderStages = std::vector<vk::PipelineShaderStageCreateInfo>(
    { vk::PipelineShaderStageCreateInfo(
        {},                                   // vk::PipelineShaderStageCreateFlags flags_ = {},
        { vk::ShaderStageFlagBits::eVertex }, // vk::ShaderStageFlagBits stage_ = vk::ShaderStageFlagBits::eVertex,
        vertexShaderModule.get(),             // vk::ShaderModule module_ = {},
        "main",                               // const char* pName_ = {},
        nullptr                               // const vk::SpecializationInfo* pSpecializationInfo_ = {}
        ),
      vk::PipelineShaderStageCreateInfo(
        {},                                     // vk::PipelineShaderStageCreateFlags flags_ = {},
        { vk::ShaderStageFlagBits::eFragment }, // vk::ShaderStageFlagBits stage_ = vk::ShaderStageFlagBits::eVertex,
        fragmentShaderModule.get(),             // vk::ShaderModule module_ = {},
        "main",                                 // const char* pName_ = {},
        nullptr                                 // const vk::SpecializationInfo* pSpecializationInfo_ = {}
        ) });

  auto vertexBindingDescriptions = std::vector<vk::VertexInputBindingDescription>({ vk::VertexInputBindingDescription(
    0,                           // uint32_t binding_ = {},
    sizeof(VertexData),          // uint32_t stride_ = {},
    vk::VertexInputRate::eVertex // vk::VertexInputRate inputRate_ = vk::VertexInputRate::eVertex
    ) });

  auto vertexInputAttributes = std::vector<vk::VertexInputAttributeDescription>(
    { vk::VertexInputAttributeDescription(
        0,                                           // uint32_t location_ = {},
        vertexBindingDescriptions[0].binding,        // uint32_t binding_ = {},
        vk::Format::eR32G32B32A32Sfloat,             // vk::Format format_ = vk::Format::eUndefined,
        offsetof(VertexData, VertexData::m_Position) // uint32_t offset_ = {}
        ),
      vk::VertexInputAttributeDescription(1,                                    // uint32_t location_ = {},
                                          vertexBindingDescriptions[0].binding, // uint32_t binding_ = {},
                                          vk::Format::eR32G32Sfloat, // vk::Format format_ = vk::Format::eUndefined,
                                          offsetof(VertexData, VertexData::m_TexCoord) // uint32_t offset_ = {}
                                          ) });

  auto vertexInputState = vk::PipelineVertexInputStateCreateInfo(
    {}, // vk::PipelineVertexInputStateCreateFlags flags_ = {}, reserved
    static_cast<uint32_t>(vertexBindingDescriptions.size()), // uint32_t vertexBindingDescriptionCount_ = {},
    vertexBindingDescriptions.data(), // const vk::VertexInputBindingDescription* pVertexBindingDescriptions_ = {},
    static_cast<uint32_t>(vertexInputAttributes.size()), // uint32_t vertexAttributeDescriptionCount_ = {},
    vertexInputAttributes.data() // const vk::VertexInputAttributeDescription* pVertexAttributeDescriptions_ = {}
  );

  auto inputAssemblyState = vk::PipelineInputAssemblyStateCreateInfo(
    {},                                    // vk::PipelineInputAssemblyStateCreateFlags flags_ = {}, reserved
    vk::PrimitiveTopology::eTriangleStrip, // vk::PrimitiveTopology topology_ = vk::PrimitiveTopology::ePointList,
    VK_FALSE                               // vk::Bool32 primitiveRestartEnable_ = {}
  );

  auto viewPortState =
    vk::PipelineViewportStateCreateInfo({},      // vk::PipelineViewportStateCreateFlags flags_ = {}, reserved
                                        1,       // uint32_t viewportCount_ = {},
                                        nullptr, // const vk::Viewport* pViewports_ = {},
                                        1,       // uint32_t scissorCount_ = {},
                                        nullptr  // const vk::Rect2D* pScissors_ = {}
    );

  auto rasterizationState = vk::PipelineRasterizationStateCreateInfo(
    {},                               // vk::PipelineRasterizationStateCreateFlags flags_ = {}, reserved
    VK_FALSE,                         // vk::Bool32 depthClampEnable_ = {},
    VK_FALSE,                         // vk::Bool32 rasterizerDiscardEnable_ = {},
    vk::PolygonMode::eFill,           // vk::PolygonMode polygonMode_ = vk::PolygonMode::eFill,
    { vk::CullModeFlagBits::eBack },  // vk::CullModeFlags cullMode_ = {},
    vk::FrontFace::eCounterClockwise, // vk::FrontFace frontFace_ = vk::FrontFace::eCounterClockwise,
    VK_FALSE,                         // vk::Bool32 depthBiasEnable_ = {},
    0.0f,                             // float depthBiasConstantFactor_ = {},
    0.0f,                             // float depthBiasClamp_ = {},
    1.0f,                             // float depthBiasSlopeFactor_ = {},
    1.0f                              // float lineWidth_ = {}
  );

  auto multisampleState = vk::PipelineMultisampleStateCreateInfo(
    {},                              // vk::PipelineMultisampleStateCreateFlags flags_ = {}, reserved
    { vk::SampleCountFlagBits::e1 }, // vk::SampleCountFlagBits rasterizationSamples_ = vk::SampleCountFlagBits::e1,
    VK_FALSE,                        // vk::Bool32 sampleShadingEnable_ = {},
    1.0f,                            // float minSampleShading_ = {},
    nullptr,                         // const vk::SampleMask* pSampleMask_ = {},
    VK_FALSE,                        // vk::Bool32 alphaToCoverageEnable_ = {},
    VK_FALSE                         // vk::Bool32 alphaToOneEnable_ = {}
  );

  auto colorBlendAttachmentState = vk::PipelineColorBlendAttachmentState(
    VK_FALSE,               // vk::Bool32 blendEnable_ = {},
    vk::BlendFactor::eOne,  // vk::BlendFactor srcColorBlendFactor_ = vk::BlendFactor::eZero,
    vk::BlendFactor::eZero, // vk::BlendFactor dstColorBlendFactor_ = vk::BlendFactor::eZero,
    vk::BlendOp::eAdd,      // vk::BlendOp colorBlendOp_ = vk::BlendOp::eAdd,
    vk::BlendFactor::eOne,  // vk::BlendFactor srcAlphaBlendFactor_ = vk::BlendFactor::eZero,
    vk::BlendFactor::eZero, // vk::BlendFactor dstAlphaBlendFactor_ = vk::BlendFactor::eZero,
    vk::BlendOp::eAdd,      // vk::BlendOp alphaBlendOp_ = vk::BlendOp::eAdd,
    { vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB
      | vk::ColorComponentFlagBits::eA } // vk::ColorComponentFlags colorWriteMask_ = {}
  );

  auto colorBlendState = vk::PipelineColorBlendStateCreateInfo(
    {},                         // vk::PipelineColorBlendStateCreateFlags flags_ = {}, reserved
    VK_FALSE,                   // vk::Bool32 logicOpEnable_ = {},
    vk::LogicOp::eCopy,         // vk::LogicOp logicOp_ = vk::LogicOp::eClear,
    1,                          // uint32_t attachmentCount_ = {},
    &colorBlendAttachmentState, // const vk::PipelineColorBlendAttachmentState* pAttachments_ = {},
    { 0.0f, 0.0f, 0.0f, 0.0f }  // std::array<float,4> const& blendConstants_ = {}
  );

  auto dynamicStates = std::vector<vk::DynamicState>({ vk::DynamicState::eViewport, vk::DynamicState::eScissor });

  auto dynamicState =
    vk::PipelineDynamicStateCreateInfo({}, // vk::PipelineDynamicStateCreateFlags flags_ = {}, reserved
                                       static_cast<uint32_t>(dynamicStates.size()), // uint32_t dynamicStateCount_ = {},
                                       dynamicStates.data() // const vk::DynamicState* pDynamicStates_ = {}
    );

  m_VulkanParameters.m_PipelineLayout = CreatePipelineLayout();

  auto pipelineCreateInfo = vk::GraphicsPipelineCreateInfo(
    {},                                         // vk::PipelineCreateFlags flags_ = {},
    static_cast<uint32_t>(shaderStages.size()), // uint32_t stageCount_ = {},
    shaderStages.data(),                        // const vk::PipelineShaderStageCreateInfo* pStages_ = {},
    &vertexInputState,                   // const vk::PipelineVertexInputStateCreateInfo* pVertexInputState_ = {},
    &inputAssemblyState,                 // const vk::PipelineInputAssemblyStateCreateInfo* pInputAssemblyState_ = {},
    nullptr,                             // const vk::PipelineTessellationStateCreateInfo* pTessellationState_ = {},
    &viewPortState,                      // const vk::PipelineViewportStateCreateInfo* pViewportState_ = {},
    &rasterizationState,                 // const vk::PipelineRasterizationStateCreateInfo* pRasterizationState_ = {},
    &multisampleState,                   // const vk::PipelineMultisampleStateCreateInfo* pMultisampleState_ = {},
    nullptr,                             // const vk::PipelineDepthStencilStateCreateInfo* pDepthStencilState_ = {},
    &colorBlendState,                    // const vk::PipelineColorBlendStateCreateInfo* pColorBlendState_ = {},
    &dynamicState,                       // const vk::PipelineDynamicStateCreateInfo* pDynamicState_ = {},
    m_VulkanParameters.m_PipelineLayout, // vk::PipelineLayout layout_ = {},
    m_VulkanParameters.m_RenderPass,     // vk::RenderPass renderPass_ = {},
    0,                                   // uint32_t subpass_ = {},
    nullptr,                             // vk::Pipeline basePipelineHandle_ = {},
    -1                                   // int32_t basePipelineIndex_ = {}
  );

  m_VulkanParameters.m_Pipeline = m_VulkanParameters.m_Device.createGraphicsPipeline(nullptr, pipelineCreateInfo);
  return true;
}

vk::UniqueShaderModule VulkanRenderer::CreateShaderModule(char const* filename)
{
  std::vector<char> code = Os::ReadContentFromBinaryFile(filename);
  if (code.empty()) {
    throw std::runtime_error("Could not read shader file or its empty");
  }
  auto shaderModuleCreateInfo =
    vk::ShaderModuleCreateInfo({},          // vk::ShaderModuleCreateFlags flags_ = {}, reserved
                               code.size(), // size_t codeSize_ = {},
                               reinterpret_cast<uint32_t const*>(code.data()) // const uint32_t* pCode_ = {}
    );

  return m_VulkanParameters.m_Device.createShaderModuleUnique(shaderModuleCreateInfo);
}

vk::PipelineLayout VulkanRenderer::CreatePipelineLayout()
{
  auto pipelineLayoutCreateInfo = vk::PipelineLayoutCreateInfo(
    {},                                        // vk::PipelineLayoutCreateFlags flags_ = {}, reserved
    1,                                         // uint32_t setLayoutCount_ = {},
    &m_VulkanParameters.m_DescriptorSetLayout, // const vk::DescriptorSetLayout* pSetLayouts_ = {},
    0,                                         // uint32_t pushConstantRangeCount_ = {},
    nullptr                                    // const vk::PushConstantRange* pPushConstantRanges_ = {}
  );

  return m_VulkanParameters.m_Device.createPipelineLayout(pipelineLayoutCreateInfo);
}

bool VulkanRenderer::RecreateSwapchain()
{
  m_VulkanParameters.m_Device.waitIdle();

  for (auto& imageView : m_VulkanParameters.m_Swapchain.m_ImageViews) {
    m_VulkanParameters.m_Device.destroyImageView(imageView);
  }

  if (m_VulkanParameters.m_Swapchain.m_Handle) {
    m_VulkanParameters.m_Device.destroySwapchainKHR(m_VulkanParameters.m_Swapchain.m_Handle);
    m_VulkanParameters.m_Swapchain.m_Handle = nullptr;
  }

  if (!CreateSwapchain()) {
    return false;
  }
  return true;
}

BufferData VulkanRenderer::CreateBuffer(vk::DeviceSize size,
                                        vk::BufferUsageFlags usage,
                                        vk::MemoryPropertyFlags requiredProperties)
{
  auto bufferCreateInfo =
    vk::BufferCreateInfo({},                          // vk::BufferCreateFlags flags_ = {},
                         size,                        // vk::DeviceSize size_ = {},
                         usage,                       // vk::BufferUsageFlags usage_ = {},
                         vk::SharingMode::eExclusive, // vk::SharingMode sharingMode_ = vk::SharingMode::eExclusive,
                         0,                           // uint32_t queueFamilyIndexCount_ = {},
                         nullptr                      // const uint32_t* pQueueFamilyIndices_ = {}
    );

  BufferData buffer;
  buffer.m_Handle = m_VulkanParameters.m_Device.createBuffer(bufferCreateInfo);

  vk::MemoryRequirements memoryRequirements = m_VulkanParameters.m_Device.getBufferMemoryRequirements(buffer.m_Handle);
  vk::PhysicalDeviceMemoryProperties memoryProperties = m_VulkanParameters.m_PhysicalDevice.getMemoryProperties();
  for (uint32_t i = 0; i != memoryProperties.memoryTypeCount; ++i) {
    if (memoryRequirements.memoryTypeBits & (1 << i)
        && (memoryProperties.memoryTypes[i].propertyFlags & requiredProperties)) {
      auto allocateInfo = vk::MemoryAllocateInfo(memoryRequirements.size, // vk::DeviceSize allocationSize_ = {},
                                                 i                        // uint32_t memoryTypeIndex_ = {}
      );

      buffer.m_Size = memoryRequirements.size;
      buffer.m_Memory = m_VulkanParameters.m_Device.allocateMemory(allocateInfo);
      break;
    }
  }

  m_VulkanParameters.m_Device.bindBufferMemory(buffer.m_Handle, buffer.m_Memory, 0);
  return buffer;
}

void VulkanRenderer::SubmitToGraphicsQueue(vk::SubmitInfo& submitInfo, vk::Fence fence)
{
  std::lock_guard<std::mutex> lock(m_GraphicsQueueSubmitCriticalSection);
  m_VulkanParameters.m_GraphicsQueue.submit(submitInfo, fence);
}

void VulkanRenderer::SubmitToTransferQueue(vk::SubmitInfo& submitInfo, vk::Fence fence)
{
  std::lock_guard<std::mutex> lock(m_TransferQueueSubmitCriticalSection);
  m_VulkanParameters.m_TransferQueue.submit(submitInfo, fence);
}

void VulkanRenderer::FreeBuffer(BufferData& buffer)
{
  m_VulkanParameters.m_Device.waitIdle();
  if (buffer.m_Memory) {
    m_VulkanParameters.m_Device.freeMemory(buffer.m_Memory);
  }
  if (buffer.m_Handle) {
    m_VulkanParameters.m_Device.destroyBuffer(buffer.m_Handle);
  }
}

bool VulkanRenderer::CreateFramebuffer(vk::Framebuffer& framebuffer, vk::ImageView& imageView)
{
  if (framebuffer) {
    m_VulkanParameters.m_Device.destroyFramebuffer(framebuffer);
    framebuffer = nullptr;
  }

  auto framebufferCreateInfo =
    vk::FramebufferCreateInfo({},                              // vk::FramebufferCreateFlags flags_ = {},
                              m_VulkanParameters.m_RenderPass, // vk::RenderPass renderPass_ = {},
                              1,                               // uint32_t attachmentCount_ = {},
                              &imageView,                      // const vk::ImageView* pAttachments_ = {},
                              m_VulkanParameters.m_Swapchain.m_ImageExtent.width,  // uint32_t width_ = {},
                              m_VulkanParameters.m_Swapchain.m_ImageExtent.height, // uint32_t height_ = {},
                              1                                                    // uint32_t layers_ = {}
    );

  framebuffer = m_VulkanParameters.m_Device.createFramebuffer(framebufferCreateInfo);
  return true;
}

void VulkanRenderer::CopyToLocalBuffer(std::shared_ptr<CopyToLocalBufferJob> transferJob,
                                       vk::CommandBuffer graphicsCommandBuffer,
                                       vk::CommandBuffer transferCommandBuffer,
                                       vk::Buffer sourceBuffer,
                                       vk::DeviceSize sourceOffset)
{
  transferCommandBuffer.begin(vk::CommandBufferBeginInfo({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit }, nullptr));
  auto copyRegion = vk::BufferCopy(sourceOffset,                        // vk::DeviceSize srcOffset_ = {},
                                   transferJob->GetDestinationOffset(), // vk::DeviceSize dstOffset_ = {},
                                   transferJob->GetSize()               // vk::DeviceSize size_ = {}
  );
  transferCommandBuffer.copyBuffer(sourceBuffer, transferJob->GetDestinationBuffer(), copyRegion);

  // Release ownership
  auto releaseBarrier =
    vk::BufferMemoryBarrier({ vk::AccessFlagBits::eTransferWrite },      // vk::AccessFlags srcAccessMask_ = {},
                            {},                                          // vk::AccessFlags dstAccessMask_ = {},
                            m_VulkanParameters.m_TransferQueueFamilyIdx, // uint32_t srcQueueFamilyIndex_ = {},
                            m_VulkanParameters.m_GraphicsQueueFamilyIdx, // uint32_t dstQueueFamilyIndex_ = {},
                            transferJob->GetDestinationBuffer(),         // vk::Buffer buffer_ = {},
                            transferJob->GetDestinationOffset(),         // vk::DeviceSize offset_ = {},
                            transferJob->GetSize()                       // vk::DeviceSize size_ = {}
    );
  transferCommandBuffer.pipelineBarrier({ vk::PipelineStageFlagBits::eTransfer },
                                        { vk::PipelineStageFlagBits::eBottomOfPipe },
                                        {},
                                        nullptr,
                                        releaseBarrier,
                                        nullptr);
  transferCommandBuffer.end();

  auto transferSubmitInfo =
    vk::SubmitInfo(0,                      // uint32_t waitSemaphoreCount_ = {},
                   nullptr,                // const vk::Semaphore* pWaitSemaphores_ = {},
                   nullptr,                // const vk::PipelineStageFlags* pWaitDstStageMask_ = {},
                   1,                      // uint32_t commandBufferCount_ = {},
                   &transferCommandBuffer, // const vk::CommandBuffer* pCommandBuffers_ = {},
                   1,                      // uint32_t signalSemaphoreCount_ = {},
                   &transferJob->GetFromTransferToGraphicsSemaphore() // const vk::Semaphore* pSignalSemaphores_ = {}
    );
  SubmitToTransferQueue(transferSubmitInfo, nullptr);

  graphicsCommandBuffer.begin(vk::CommandBufferBeginInfo({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit }, nullptr));
  // Acquire ownership
  auto acquireBarrier =
    vk::BufferMemoryBarrier({},                                          // vk::AccessFlags srcAccessMask_ = {},
                            transferJob->GetDestinationAccessFlags(),    // vk::AccessFlags dstAccessMask_ = {},
                            m_VulkanParameters.m_TransferQueueFamilyIdx, // uint32_t srcQueueFamilyIndex_ = {},
                            m_VulkanParameters.m_GraphicsQueueFamilyIdx, // uint32_t dstQueueFamilyIndex_ = {},
                            transferJob->GetDestinationBuffer(),         // vk::Buffer buffer_ = {},
                            transferJob->GetDestinationOffset(),         // vk::DeviceSize offset_ = {},
                            transferJob->GetSize()                       // vk::DeviceSize size_ = {}
    );

  graphicsCommandBuffer.pipelineBarrier({ vk::PipelineStageFlagBits::eTopOfPipe },
                                        transferJob->GetDestinationPipelineStageFlags(),
                                        {},
                                        nullptr,
                                        acquireBarrier,
                                        nullptr);
  graphicsCommandBuffer.end();

  vk::PipelineStageFlags waitStage[] = { transferJob->GetDestinationPipelineStageFlags() };
  auto graphicsSubmitInfo =
    vk::SubmitInfo(1,                                                  // uint32_t waitSemaphoreCount_ = {},
                   &transferJob->GetFromTransferToGraphicsSemaphore(), // const vk::Semaphore* pWaitSemaphores_ = {},
                   waitStage,              // const vk::PipelineStageFlags* pWaitDstStageMask_ = {},
                   1,                      // uint32_t commandBufferCount_ = {},
                   &graphicsCommandBuffer, // const vk::CommandBuffer* pCommandBuffers_ = {},
                   0,                      // uint32_t signalSemaphoreCount_ = {},
                   nullptr                 // const vk::Semaphore* pSignalSemaphores_ = {}
    );
  SubmitToGraphicsQueue(graphicsSubmitInfo, transferJob->GetTransferCompletedFence());
  transferJob->SetWait();
}

void VulkanRenderer::CopyToLocalImage(std::shared_ptr<Core::CopyToLocalImageJob> transferJob,
                                      vk::CommandBuffer graphicsCommandBuffer,
                                      vk::CommandBuffer transferCommandBuffer,
                                      vk::Buffer sourceBuffer,
                                      vk::DeviceSize sourceOffset)
{
  transferCommandBuffer.begin(vk::CommandBufferBeginInfo({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit }, nullptr));
  auto region = vk::BufferImageCopy(
    sourceOffset,                                               // vk::DeviceSize bufferOffset_ = {},
    0,                                                          // uint32_t bufferRowLength_ = {},
    0,                                                          // uint32_t bufferImageHeight_ = {},
    vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, // vk::ImageAspectFlags aspectMask_ = {},
                               0,                               // uint32_t mipLevel_ = {},
                               0,                               // uint32_t baseArrayLayer_ = {},
                               1                                // uint32_t layerCount_ = {}
                               ),                               // vk::ImageSubresourceLayers imageSubresource_ = {},
    vk::Offset3D(0, 0, 0),                                      // vk::Offset3D imageOffset_ = {},
    vk::Extent3D(transferJob->GetImageWidth(), transferJob->GetImageHeight(), 1) // vk::Extent3D imageExtent_ = {}
  );

  auto fromUndefinedToTransferDstLayoutBarrier = vk::ImageMemoryBarrier(
    {},                                          // vk::AccessFlags srcAccessMask_ = {},
    vk::AccessFlagBits::eTransferWrite,          // vk::AccessFlags dstAccessMask_ = {},
    vk::ImageLayout::eUndefined,                 // vk::ImageLayout oldLayout_ = vk::ImageLayout::eUndefined,
    vk::ImageLayout::eTransferDstOptimal,        // vk::ImageLayout newLayout_ = vk::ImageLayout::eUndefined,
    m_VulkanParameters.m_TransferQueueFamilyIdx, // uint32_t srcQueueFamilyIndex_ = {},
    m_VulkanParameters.m_TransferQueueFamilyIdx, // uint32_t dstQueueFamilyIndex_ = {},
    transferJob->GetDestinationImage(),          // vk::Image image_ = {},
    vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, // vk::ImageAspectFlags aspectMask_ = {},
                              0,                               // uint32_t baseMipLevel_ = {},
                              1,                               // uint32_t levelCount_ = {},
                              0,                               // uint32_t baseArrayLayer_ = {},
                              1                                // uint32_t layerCount_ = {}
                              )                                // vk::ImageSubresourceRange subresourceRange_ = {}
  );

  transferCommandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                                        vk::PipelineStageFlagBits::eTransfer,
                                        {},
                                        nullptr,
                                        nullptr,
                                        fromUndefinedToTransferDstLayoutBarrier);

  transferCommandBuffer.copyBufferToImage(
    sourceBuffer, transferJob->GetDestinationImage(), vk::ImageLayout::eTransferDstOptimal, region);

  auto releaseBarrier = vk::ImageMemoryBarrier(
    vk::AccessFlagBits::eTransferWrite,          // vk::AccessFlags srcAccessMask_ = {},
    {},                                          // vk::AccessFlags dstAccessMask_ = {},
    vk::ImageLayout::eTransferDstOptimal,        // vk::ImageLayout oldLayout_ = vk::ImageLayout::eUndefined,
    transferJob->GetDestinationLayout(),         // vk::ImageLayout newLayout_ = vk::ImageLayout::eUndefined,
    m_VulkanParameters.m_TransferQueueFamilyIdx, // uint32_t srcQueueFamilyIndex_ = {},
    m_VulkanParameters.m_GraphicsQueueFamilyIdx, // uint32_t dstQueueFamilyIndex_ = {},
    transferJob->GetDestinationImage(),          // vk::Image image_ = {},
    vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, // vk::ImageAspectFlags aspectMask_ = {},
                              0,                               // uint32_t baseMipLevel_ = {},
                              1,                               // uint32_t levelCount_ = {},
                              0,                               // uint32_t baseArrayLayer_ = {},
                              1                                // uint32_t layerCount_ = {}
                              )                                // vk::ImageSubresourceRange subresourceRange_ = {}
  );
  transferCommandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                        vk::PipelineStageFlagBits::eBottomOfPipe,
                                        {},
                                        nullptr,
                                        nullptr,
                                        releaseBarrier);

  transferCommandBuffer.end();
  auto transferSubmitInfo =
    vk::SubmitInfo(0,                      // uint32_t waitSemaphoreCount_ = {},
                   nullptr,                // const vk::Semaphore* pWaitSemaphores_ = {},
                   nullptr,                // const vk::PipelineStageFlags* pWaitDstStageMask_ = {},
                   1,                      // uint32_t commandBufferCount_ = {},
                   &transferCommandBuffer, // const vk::CommandBuffer* pCommandBuffers_ = {},
                   1,                      // uint32_t signalSemaphoreCount_ = {},
                   &transferJob->GetFromTransferToGraphicsSemaphore() // const vk::Semaphore* pSignalSemaphores_ = {}
    );
  SubmitToTransferQueue(transferSubmitInfo, nullptr);

  graphicsCommandBuffer.begin(vk::CommandBufferBeginInfo({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit }, nullptr));

  auto acquireBarrier = vk::ImageMemoryBarrier(
    {},                                          // vk::AccessFlags srcAccessMask_ = {},
    transferJob->GetDestinationAccessFlags(),    // vk::AccessFlags dstAccessMask_ = {},
    vk::ImageLayout::eTransferDstOptimal,        // vk::ImageLayout oldLayout_ = vk::ImageLayout::eUndefined,
    transferJob->GetDestinationLayout(),         // vk::ImageLayout newLayout_ = vk::ImageLayout::eUndefined,
    m_VulkanParameters.m_TransferQueueFamilyIdx, // uint32_t srcQueueFamilyIndex_ = {},
    m_VulkanParameters.m_GraphicsQueueFamilyIdx, // uint32_t dstQueueFamilyIndex_ = {},
    transferJob->GetDestinationImage(),          // vk::Image image_ = {},
    vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, // vk::ImageAspectFlags aspectMask_ = {},
                              0,                               // uint32_t baseMipLevel_ = {},
                              1,                               // uint32_t levelCount_ = {},
                              0,                               // uint32_t baseArrayLayer_ = {},
                              1                                // uint32_t layerCount_ = {}
                              )                                // vk::ImageSubresourceRange subresourceRange_ = {}
  );
  graphicsCommandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                                        transferJob->GetDestinationPipelineStageFlags(),
                                        {},
                                        nullptr,
                                        nullptr,
                                        acquireBarrier);

  graphicsCommandBuffer.end();

  vk::PipelineStageFlags waitStage[] = { transferJob->GetDestinationPipelineStageFlags() };
  auto graphicsSubmitInfo =
    vk::SubmitInfo(1,                                                  // uint32_t waitSemaphoreCount_ = {},
                   &transferJob->GetFromTransferToGraphicsSemaphore(), // const vk::Semaphore* pWaitSemaphores_ = {},
                   waitStage,              // const vk::PipelineStageFlags* pWaitDstStageMask_ = {},
                   1,                      // uint32_t commandBufferCount_ = {},
                   &graphicsCommandBuffer, // const vk::CommandBuffer* pCommandBuffers_ = {},
                   0,                      // uint32_t signalSemaphoreCount_ = {},
                   nullptr                 // const vk::Semaphore* pSignalSemaphores_ = {}
    );

  SubmitToGraphicsQueue(graphicsSubmitInfo, transferJob->GetTransferCompletedFence());
  transferJob->SetWait();
}

vk::DeviceSize VulkanRenderer::GetNonCoherentAtomSize() const
{
  vk::PhysicalDeviceProperties2 properties = m_VulkanParameters.m_PhysicalDevice.getProperties2();
  return properties.properties.limits.nonCoherentAtomSize;
}

ImageData VulkanRenderer::CreateImage(uint32_t width,
                                      uint32_t height,
                                      vk::ImageUsageFlags usage,
                                      vk::MemoryPropertyFlags requiredProperties)
{
  vk::Format imageFormat = vk::Format::eR8G8B8A8Unorm;
  auto imageCreateInfo =
    vk::ImageCreateInfo({},                             // vk::ImageCreateFlags flags_ = {},
                        vk::ImageType::e2D,             // vk::ImageType imageType_ = vk::ImageType::e1D,
                        imageFormat,                    // vk::Format format_ = vk::Format::eUndefined,
                        vk::Extent3D(width, height, 1), // vk::Extent3D extent_ = {},
                        1,                              // uint32_t mipLevels_ = {},
                        1,                              // uint32_t arrayLayers_ = {},
                        vk::SampleCountFlagBits::e1, // vk::SampleCountFlagBits samples_ = vk::SampleCountFlagBits::e1,
                        vk::ImageTiling::eOptimal,   // vk::ImageTiling tiling_ = vk::ImageTiling::eOptimal,
                        usage,                       // vk::ImageUsageFlags usage_ = {},
                        vk::SharingMode::eExclusive, // vk::SharingMode sharingMode_ = vk::SharingMode::eExclusive,
                        0,                           // uint32_t queueFamilyIndexCount_ = {},
                        nullptr,                     // const uint32_t* pQueueFamilyIndices_ = {},
                        vk::ImageLayout::eUndefined  // vk::ImageLayout initialLayout_ = vk::ImageLayout::eUndefined
    );

  ImageData image;
  image.m_Handle = m_VulkanParameters.m_Device.createImage(imageCreateInfo);
  image.m_Width = width;
  image.m_Height = height;

  vk::MemoryRequirements memoryRequirements = m_VulkanParameters.m_Device.getImageMemoryRequirements(image.m_Handle);
  vk::PhysicalDeviceMemoryProperties memoryProperties = m_VulkanParameters.m_PhysicalDevice.getMemoryProperties();
  for (uint32_t i = 0; i != memoryProperties.memoryTypeCount; ++i) {
    if (memoryRequirements.memoryTypeBits & (1 << i)
        && (memoryProperties.memoryTypes[i].propertyFlags & requiredProperties)) {
      auto allocateInfo = vk::MemoryAllocateInfo(memoryRequirements.size, // vk::DeviceSize allocationSize_ = {},
                                                 i                        // uint32_t memoryTypeIndex_ = {}
      );

      image.m_Memory = m_VulkanParameters.m_Device.allocateMemory(allocateInfo);
      break;
    }
  }

  m_VulkanParameters.m_Device.bindImageMemory(image.m_Handle, image.m_Memory, vk::DeviceSize(0));

  auto imageViewCreateInfo = vk::ImageViewCreateInfo(
    {},                     // vk::ImageViewCreateFlags flags_ = {},
    image.m_Handle,         // vk::Image image_ = {},
    vk::ImageViewType::e2D, // vk::ImageViewType viewType_ = vk::ImageViewType::e1D,
    imageFormat,            // vk::Format format_ = vk::Format::eUndefined,
    vk::ComponentMapping(vk::ComponentSwizzle::eIdentity,
                         vk::ComponentSwizzle::eIdentity,
                         vk::ComponentSwizzle::eIdentity,
                         vk::ComponentSwizzle::eIdentity), // vk::ComponentMapping components_ = {},
    vk::ImageSubresourceRange(
      vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1) // vk::ImageSubresourceRange subresourceRange_ = {}
  );
  image.m_View = m_VulkanParameters.m_Device.createImageView(imageViewCreateInfo);
  return image;
}

void VulkanRenderer::FreeImage(ImageData& imageData)
{
  m_VulkanParameters.m_Device.waitIdle();
  if (imageData.m_Memory) {
    m_VulkanParameters.m_Device.freeMemory(imageData.m_Memory);
  }
  if (imageData.m_Handle) {
    m_VulkanParameters.m_Device.destroyImage(imageData.m_Handle);
  }
}

} // namespace Core
