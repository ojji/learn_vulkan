#include "CopyToLocalBufferJob.h"
#include "VulkanRenderer.h"

namespace Core {
CopyToLocalBufferJob::CopyToLocalBufferJob(Core::VulkanRenderer* renderer,
                                           void* data,
                                           vk::DeviceSize size,
                                           vk::Buffer destinationBuffer,
                                           vk::DeviceSize destinationOffset,
                                           vk::AccessFlags destinationAccessFlags,
                                           vk::PipelineStageFlags destinationPipelineStageFlags) :
  m_Renderer(renderer),
  m_Data(data),
  m_Size(size),
  m_DestinationBuffer(destinationBuffer),
  m_DestinationOffset(destinationOffset),
  m_DestinationAccessFlags(destinationAccessFlags),
  m_DestinationPipelineStageFlags(destinationPipelineStageFlags),
  m_CopyCriticalSection(std::mutex()),
  m_Cv(std::condition_variable()),
  m_ReadyToWait(false)
{
  m_FromTransferToGraphicsSemaphore = m_Renderer->GetDevice().createSemaphore(vk::SemaphoreCreateInfo({}));
  m_TransferCompletedFence = m_Renderer->GetDevice().createFence(vk::FenceCreateInfo({}));
}

CopyToLocalBufferJob::~CopyToLocalBufferJob()
{
  m_Renderer->GetDevice().destroySemaphore(m_FromTransferToGraphicsSemaphore);
  m_Renderer->GetDevice().destroyFence(m_TransferCompletedFence);
}

void CopyToLocalBufferJob::SetWait()
{
  std::lock_guard<std::mutex> lock(m_CopyCriticalSection);
  m_ReadyToWait = true;
  m_Cv.notify_all();
}

void CopyToLocalBufferJob::WaitComplete()
{
  std::unique_lock<std::mutex> lock(m_CopyCriticalSection);
  m_Cv.wait(lock, [&] { return m_ReadyToWait; });
  while (m_Renderer->GetDevice().getFenceStatus(m_TransferCompletedFence) != vk::Result::eSuccess) {}
}
} // namespace Core
