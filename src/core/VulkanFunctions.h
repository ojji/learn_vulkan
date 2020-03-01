#ifndef CORE_VULKANFUNCTIONS_H
#define CORE_VULKANFUNCTIONS_H

#include "vulkan/vulkan.h"

namespace Core {
#define VK_EXPORTED_FUNCTION( fun ) extern PFN_##fun fun;
#define VK_GLOBAL_FUNCTION( fun ) extern PFN_##fun fun;
#define VK_INSTANCE_FUNCTION( fun ) extern PFN_##fun fun;
#define VK_DEVICE_FUNCTION( fun ) extern PFN_##fun fun;
#include "VulkanFunctions.inl"
}

#endif // CORE_VULKANFUNCTIONS_H
