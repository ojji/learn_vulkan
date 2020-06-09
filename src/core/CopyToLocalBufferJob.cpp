#include "CopyToLocalBufferJob.h"
#include "VulkanRenderer.h"

namespace Core {
CopyToLocalBufferJob::CopyToLocalBufferJob(Core::VulkanRenderer* renderer,
                                           void* data,
                                           vk::DeviceSize size,
                                           vk::Buffer destinationBuffer,
                                           vk::DeviceSize destinationOffset,
                                           vk::AccessFlags destinationAccessFlags,
                                           vk::PipelineStageFlags destinationPipelineStageFlags,
                                           vk::Fence canCleanupFence) :
  CopyToLocalJob(renderer, data, size, CopyFlags::ToLocalBuffer, canCleanupFence),
  m_DestinationBuffer(destinationBuffer),
  m_DestinationOffset(destinationOffset),
  m_DestinationAccessFlags(destinationAccessFlags),
  m_DestinationPipelineStageFlags(destinationPipelineStageFlags)
{}

CopyToLocalBufferJob::~CopyToLocalBufferJob()
{}

} // namespace Core
