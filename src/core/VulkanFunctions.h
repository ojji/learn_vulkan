#ifndef CORE_VULKANFUNCTIONS_H
#define CORE_VULKANFUNCTIONS_H

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

#endif // CORE_VULKANFUNCTIONS_H
