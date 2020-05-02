#ifndef VK_EXPORTED_FUNCTION
#define VK_EXPORTED_FUNCTION(fun)
#endif

VK_EXPORTED_FUNCTION(vkGetInstanceProcAddr)

#undef VK_EXPORTED_FUNCTION

#ifndef VK_GLOBAL_FUNCTION
#define VK_GLOBAL_FUNCTION(fun)
#endif

VK_GLOBAL_FUNCTION(vkCreateInstance)
VK_GLOBAL_FUNCTION(vkEnumerateInstanceVersion)
VK_GLOBAL_FUNCTION(vkEnumerateInstanceExtensionProperties)
VK_GLOBAL_FUNCTION(vkEnumerateInstanceLayerProperties)

#undef VK_GLOBAL_FUNCTION

#ifndef VK_INSTANCE_FUNCTION
#define VK_INSTANCE_FUNCTION(fun)
#endif

VK_INSTANCE_FUNCTION(vkEnumeratePhysicalDevices)
VK_INSTANCE_FUNCTION(vkEnumerateDeviceExtensionProperties)

VK_INSTANCE_FUNCTION(vkGetPhysicalDeviceProperties2)
VK_INSTANCE_FUNCTION(vkGetPhysicalDeviceFeatures2)
VK_INSTANCE_FUNCTION(vkGetPhysicalDeviceQueueFamilyProperties2)
VK_INSTANCE_FUNCTION(vkGetPhysicalDeviceMemoryProperties)
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
#define VK_DEVICE_FUNCTION(fun)
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
VK_DEVICE_FUNCTION(vkFreeCommandBuffers)
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

// Render pass and buffers
VK_DEVICE_FUNCTION(vkCreateRenderPass)
VK_DEVICE_FUNCTION(vkDestroyRenderPass)
VK_DEVICE_FUNCTION(vkCreateFramebuffer)
VK_DEVICE_FUNCTION(vkDestroyFramebuffer)
VK_DEVICE_FUNCTION(vkCreateBuffer)
VK_DEVICE_FUNCTION(vkGetBufferMemoryRequirements)
VK_DEVICE_FUNCTION(vkBindBufferMemory)
VK_DEVICE_FUNCTION(vkDestroyBuffer)
VK_DEVICE_FUNCTION(vkCreateImageView)
VK_DEVICE_FUNCTION(vkDestroyImageView)
VK_DEVICE_FUNCTION(vkCmdBeginRenderPass)
VK_DEVICE_FUNCTION(vkCmdEndRenderPass)

// Shaders
VK_DEVICE_FUNCTION(vkCreateShaderModule)
VK_DEVICE_FUNCTION(vkDestroyShaderModule)

// Pipelines
VK_DEVICE_FUNCTION(vkCreateGraphicsPipelines)
VK_DEVICE_FUNCTION(vkDestroyPipeline)
VK_DEVICE_FUNCTION(vkCreatePipelineLayout)
VK_DEVICE_FUNCTION(vkDestroyPipelineLayout)
VK_DEVICE_FUNCTION(vkCmdBindPipeline)
VK_DEVICE_FUNCTION(vkCmdBindVertexBuffers)

// Draw
VK_DEVICE_FUNCTION(vkCmdDraw)

// Memory
VK_DEVICE_FUNCTION(vkAllocateMemory)
VK_DEVICE_FUNCTION(vkMapMemory)
VK_DEVICE_FUNCTION(vkUnmapMemory)
VK_DEVICE_FUNCTION(vkFreeMemory)
VK_DEVICE_FUNCTION(vkFlushMappedMemoryRanges)

// Fixed-Function Vertex Post-Processing
VK_DEVICE_FUNCTION(vkCmdSetScissor)
VK_DEVICE_FUNCTION(vkCmdSetViewport)

// Queries
VK_DEVICE_FUNCTION(vkCreateQueryPool)
VK_DEVICE_FUNCTION(vkCmdResetQueryPool)
VK_DEVICE_FUNCTION(vkGetQueryPoolResults)
VK_DEVICE_FUNCTION(vkCmdWriteTimestamp)
VK_DEVICE_FUNCTION(vkDestroyQueryPool)

// Copy commands
VK_DEVICE_FUNCTION(vkCmdCopyBuffer)

#undef VK_DEVICE_FUNCTION

#ifndef VK_EXPAND_VERSION
#define VK_EXPAND_VERSION(version)                                                                                     \
  VK_VERSION_MAJOR(version) << "." << VK_VERSION_MINOR(version) << "." << VK_VERSION_PATCH(version)
#endif

#ifndef VK_EXPAND_EXTENT2D
#define VK_EXPAND_EXTENT2D(extent2D) "(" << extent2D.width << "," << extent2D.height << ")"
#endif

#ifndef VK_EXPAND_EXTENT3D
#define VK_EXPAND_EXTENT3D(extent3D) "(" << extent3D.width << "," << extent3D.height << "," << extent3D.depth << ")"
#endif
