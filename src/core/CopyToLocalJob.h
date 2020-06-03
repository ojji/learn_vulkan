#ifndef CORE_COPYTOLOCALJOB_H
#define CORE_COPYTOLOCALJOB_H

#include <condition_variable>
#include <mutex>
#include <vulkan/vulkan.hpp>

namespace Core {
class VulkanRenderer;

enum class CopyFlags
{
  ToLocalBuffer,
  ToLocalImage
};

class CopyToLocalJob
{
public:
  void SetWait();
  void WaitComplete();
  CopyFlags GetJobType() { return m_JobType; }

  inline void* GetDataPtr() const { return m_Data; }
  inline vk::DeviceSize GetSize() const { return m_Size; }
  inline vk::Semaphore& GetFromTransferToGraphicsSemaphore() { return m_FromTransferToGraphicsSemaphore; }
  inline vk::Fence& GetTransferCompletedFence() { return m_TransferCompletedFence; }

protected:
  CopyToLocalJob(Core::VulkanRenderer* renderer, void* data, vk::DeviceSize size, CopyFlags jobType);
  virtual ~CopyToLocalJob();

  Core::VulkanRenderer* m_Renderer;
  void* m_Data;
  vk::DeviceSize m_Size;

private:
  std::mutex m_CopyCriticalSection;
  std::condition_variable m_Cv;
  bool m_ReadyToWait;
  vk::Fence m_TransferCompletedFence;
  vk::Semaphore m_FromTransferToGraphicsSemaphore;
  CopyFlags m_JobType;
};
} // namespace Core

#endif // CORE_COPYTOLOCALJOB_H