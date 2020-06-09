#ifndef CORE_COPYTOLOCALBUFFERJOB_H
#define CORE_COPYTOLOCALBUFFERJOB_H

#include "CopyToLocalJob.h"

namespace Core {

class CopyToLocalBufferJob : public CopyToLocalJob
{
public:
  CopyToLocalBufferJob(Core::VulkanRenderer* renderer,
                       void* data,
                       vk::DeviceSize size,
                       vk::Buffer destinationBuffer,
                       vk::DeviceSize destinationOffset,
                       vk::AccessFlags destinationAccessFlags,
                       vk::PipelineStageFlags destinationPipelineStageFlags,
                       vk::Fence canCleanupFence);

  virtual ~CopyToLocalBufferJob();

  vk::Buffer GetDestinationBuffer() const { return m_DestinationBuffer; }
  vk::DeviceSize GetDestinationOffset() const { return m_DestinationOffset; }
  inline vk::AccessFlags GetDestinationAccessFlags() const { return m_DestinationAccessFlags; }
  inline vk::PipelineStageFlags GetDestinationPipelineStageFlags() const { return m_DestinationPipelineStageFlags; }

private:
  vk::Buffer m_DestinationBuffer;
  vk::DeviceSize m_DestinationOffset;
  vk::AccessFlags m_DestinationAccessFlags;
  vk::PipelineStageFlags m_DestinationPipelineStageFlags;
};

} // namespace Core
#endif // CORE_COPYTOLOCALBUFFERJOB_H