#ifndef VK_EXPORTED_FUNCTION
#define VK_EXPORTED_FUNCTION( fun )
#endif

VK_EXPORTED_FUNCTION(vkGetInstanceProcAddr)

#undef VK_EXPORTED_FUNCTION

#ifndef VK_GLOBAL_FUNCTION
#define VK_GLOBAL_FUNCTION( fun )
#endif

VK_GLOBAL_FUNCTION(vkCreateInstance)
VK_GLOBAL_FUNCTION(vkEnumerateInstanceVersion)
VK_GLOBAL_FUNCTION(vkEnumerateInstanceExtensionProperties)
VK_GLOBAL_FUNCTION(vkEnumerateInstanceLayerProperties)

#undef VK_GLOBAL_FUNCTION

#ifndef VK_INSTANCE_FUNCTION
#define VK_INSTANCE_FUNCTION( fun )
#endif

VK_INSTANCE_FUNCTION(vkEnumeratePhysicalDevices)
VK_INSTANCE_FUNCTION(vkEnumerateDeviceExtensionProperties)

VK_INSTANCE_FUNCTION(vkGetPhysicalDeviceProperties2)
VK_INSTANCE_FUNCTION(vkGetPhysicalDeviceFeatures2)
VK_INSTANCE_FUNCTION(vkGetPhysicalDeviceQueueFamilyProperties2)
VK_INSTANCE_FUNCTION(vkCreateDevice)
VK_INSTANCE_FUNCTION(vkGetDeviceProcAddr)
VK_INSTANCE_FUNCTION(vkDestroyInstance)

// Surface (swapchain) extensions
VK_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfaceSupportKHR)
VK_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)
VK_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfaceFormatsKHR)
VK_INSTANCE_FUNCTION(vkGetPhysicalDeviceSurfacePresentModesKHR)
VK_INSTANCE_FUNCTION(vkDestroySurfaceKHR)

#ifdef VK_USE_PLATFORM_WIN32_KHR
VK_INSTANCE_FUNCTION(vkCreateWin32SurfaceKHR)
VK_INSTANCE_FUNCTION(vkGetPhysicalDeviceWin32PresentationSupportKHR)
#endif

#undef VK_INSTANCE_FUNCTION

#ifndef VK_DEVICE_FUNCTION
#define VK_DEVICE_FUNCTION( fun )
#endif

VK_DEVICE_FUNCTION(vkGetDeviceQueue)
VK_DEVICE_FUNCTION(vkDestroyDevice)

// Swapchain device extension
VK_DEVICE_FUNCTION(vkCreateSwapchainKHR)
VK_DEVICE_FUNCTION(vkDestroySwapchainKHR)
VK_DEVICE_FUNCTION(vkAcquireNextImageKHR)
VK_DEVICE_FUNCTION(vkGetSwapchainImagesKHR)
VK_DEVICE_FUNCTION(vkQueuePresentKHR)

// Commands
VK_DEVICE_FUNCTION(vkCreateCommandPool)
VK_DEVICE_FUNCTION(vkAllocateCommandBuffers)
VK_DEVICE_FUNCTION(vkBeginCommandBuffer)
VK_DEVICE_FUNCTION(vkEndCommandBuffer)
VK_DEVICE_FUNCTION(vkDestroyCommandPool)
VK_DEVICE_FUNCTION(vkQueueSubmit)

// Synchronization primitives
VK_DEVICE_FUNCTION(vkDeviceWaitIdle)
VK_DEVICE_FUNCTION(vkCreateSemaphore)
VK_DEVICE_FUNCTION(vkDestroySemaphore)
VK_DEVICE_FUNCTION(vkCmdPipelineBarrier)
VK_DEVICE_FUNCTION(vkCreateFence)
VK_DEVICE_FUNCTION(vkWaitForFences)
VK_DEVICE_FUNCTION(vkResetFences)
VK_DEVICE_FUNCTION(vkDestroyFence)

// Draw commands
VK_DEVICE_FUNCTION(vkCmdClearColorImage)


#undef VK_DEVICE_FUNCTION

#ifndef VK_EXPAND_VERSION
#define VK_EXPAND_VERSION( version )  \
  VK_VERSION_MAJOR(version) << "." << \
  VK_VERSION_MINOR(version) << "." << \
  VK_VERSION_PATCH(version)
#endif

#ifndef VK_EXPAND_EXTENT2D
#define VK_EXPAND_EXTENT2D( extent2D )  \
  "(" << extent2D.width <<              \
  "," << extent2D.height << ")"
#endif

#ifndef VK_EXPAND_EXTENT3D
#define VK_EXPAND_EXTENT3D( extent3D )  \
  "(" << extent3D.width <<              \
  "," << extent3D.height <<             \
  "," << extent3D.depth << ")"          
#endif
