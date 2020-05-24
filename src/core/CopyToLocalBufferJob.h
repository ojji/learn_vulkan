#ifndef CORE_COPYTOLOCALBUFFERJOB_H
#define CORE_COPYTOLOCALBUFFERJOB_H

#include <mutex>
#include <vulkan/vulkan.hpp>

namespace Core {
class VulkanRenderer;

class CopyToLocalBufferJob
{
public:
  CopyToLocalBufferJob(Core::VulkanRenderer* renderer,
                       void* data,
                       vk::DeviceSize size,
                       vk::Buffer destinationBuffer,
                       vk::DeviceSize destinationOffset,
                       vk::AccessFlags destinationAccessFlags,
                       vk::PipelineStageFlags destinationPipelineStageFlags);

  ~CopyToLocalBufferJob();

  void SetWait();
  void WaitComplete();

  inline void* GetDataPtr() const { return m_Data; }
  inline vk::DeviceSize GetSize() const { return m_Size; }
  vk::Buffer GetDestinationBuffer() const { return m_DestinationBuffer; }
  vk::DeviceSize GetDestinationOffset() const { return m_DestinationOffset; }
  inline vk::Semaphore& GetFromTransferToGraphicsSemaphore() { return m_FromTransferToGraphicsSemaphore; }
  inline vk::Fence& GetTransferCompletedFence() { return m_TransferCompletedFence; }
  inline vk::AccessFlags GetDestinationAccessFlags() const { return m_DestinationAccessFlags; }
  inline vk::PipelineStageFlags GetDestinationPipelineStageFlags() const { return m_DestinationPipelineStageFlags; }

private:
  Core::VulkanRenderer* m_Renderer;
  void* m_Data;
  vk::DeviceSize m_Size;
  vk::Buffer m_DestinationBuffer;
  vk::DeviceSize m_DestinationOffset;
  vk::AccessFlags m_DestinationAccessFlags;
  vk::PipelineStageFlags m_DestinationPipelineStageFlags;
  vk::Fence m_TransferCompletedFence;
  std::mutex m_CopyCriticalSection;
  std::condition_variable m_Cv;
  bool m_ReadyToWait;
  vk::Semaphore m_FromTransferToGraphicsSemaphore;
  vk::Buffer m_SourceBuffer;
  vk::DeviceSize m_SourceOffset;
};

} // namespace Core
#endif // CORE_COPYTOLOCALBUFFERJOB_H