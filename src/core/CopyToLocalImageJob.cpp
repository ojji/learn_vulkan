#include "CopyToLocalImageJob.h"

namespace Core {
CopyToLocalImageJob::CopyToLocalImageJob(VulkanRenderer* renderer,
                                         void* data,
                                         vk::DeviceSize size,
                                         uint32_t width,
                                         uint32_t height,
                                         vk::Image destinationImage,
                                         vk::ImageLayout destinationLayout,
                                         vk::AccessFlags destinationAccessFlags,
                                         vk::PipelineStageFlags destinationPipelineStageFlags) :
  CopyToLocalJob(renderer, data, size, CopyFlags::ToLocalImage),
  m_Width(width),
  m_Height(height),
  m_DestinationImage(destinationImage),
  m_DestinationLayout(destinationLayout),
  m_DestinationAccessFlags(destinationAccessFlags),
  m_DestinationPipelineStageFlags(destinationPipelineStageFlags)
{}
} // namespace Core
