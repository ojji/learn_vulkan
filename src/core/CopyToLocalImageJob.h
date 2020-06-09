#ifndef CORE_COPYTOLOCALIMAGEJOB_H
#define CORE_COPYTOLOCALIMAGEJOB_H

#include "CopyToLocalJob.h"

namespace Core {
class VulkanRenderer;
class CopyToLocalImageJob : public CopyToLocalJob
{
public:
  CopyToLocalImageJob(VulkanRenderer* renderer,
                      void* data,
                      vk::DeviceSize size,
                      uint32_t width,
                      uint32_t height,
                      vk::Image destinationImage,
                      vk::ImageLayout destinationLayout,
                      vk::AccessFlags destinationAccessFlags,
                      vk::PipelineStageFlags destinationPipelineStageFlags,
                      vk::Fence canCleanupFence);
  uint32_t GetImageWidth() const { return m_Width; };
  uint32_t GetImageHeight() const { return m_Height; };
  vk::Image GetDestinationImage() const { return m_DestinationImage; };
  vk::ImageLayout GetDestinationLayout() const { return m_DestinationLayout; };
  vk::AccessFlags GetDestinationAccessFlags() const { return m_DestinationAccessFlags; };
  vk::PipelineStageFlags GetDestinationPipelineStageFlags() const { return m_DestinationPipelineStageFlags; };

private:
  uint32_t m_Width;
  uint32_t m_Height;
  vk::Image m_DestinationImage;
  vk::ImageLayout m_DestinationLayout;
  vk::AccessFlags m_DestinationAccessFlags;
  vk::PipelineStageFlags m_DestinationPipelineStageFlags;
};
} // namespace Core
#endif // CORE_COPYTOLOCALIMAGEJOB_H