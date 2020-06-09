#include "CopyToLocalJob.h"
#include "VulkanRenderer.h"

namespace Core {
CopyToLocalJob::CopyToLocalJob(
  Core::VulkanRenderer* renderer, void* data, vk::DeviceSize size, CopyFlags jobType, vk::Fence canCleanupFence) :
  m_Renderer(renderer),
  m_Data(data),
  m_Size(size),
  m_CopyCriticalSection(std::mutex()),
  m_Cv(std::condition_variable()),
  m_ReadyToWait(false),
  m_JobType(jobType),
  m_CanCleanupFence(canCleanupFence)
{
  m_FromTransferToGraphicsSemaphore = m_Renderer->GetDevice().createSemaphore(vk::SemaphoreCreateInfo({}));
  m_TransferCompletedFence = m_Renderer->GetDevice().createFence(vk::FenceCreateInfo({}));
  m_TransferCompletedSemaphore = m_Renderer->GetDevice().createSemaphore(vk::SemaphoreCreateInfo({}));
}

CopyToLocalJob::~CopyToLocalJob()
{
  if (m_CanCleanupFence) {
    while ((m_Renderer->GetDevice().getFenceStatus(m_CanCleanupFence)) != vk::Result::eSuccess) {}
  }
  if (m_FromTransferToGraphicsSemaphore) {
    m_Renderer->GetDevice().destroySemaphore(m_FromTransferToGraphicsSemaphore);
  }
  if (m_TransferCompletedFence) {
    m_Renderer->GetDevice().destroyFence(m_TransferCompletedFence);
  }
  if (m_TransferCompletedSemaphore) {
    m_Renderer->GetDevice().destroySemaphore(m_TransferCompletedSemaphore);
  }
}

void CopyToLocalJob::SetWait()
{
  std::lock_guard<std::mutex> lock(m_CopyCriticalSection);
  m_ReadyToWait = true;
  m_Cv.notify_all();
}

void CopyToLocalJob::WaitComplete()
{
  std::unique_lock<std::mutex> lock(m_CopyCriticalSection);
  m_Cv.wait(lock, [&] { return m_ReadyToWait; });
  while ((m_Renderer->GetDevice().getFenceStatus(m_TransferCompletedFence)) != vk::Result::eSuccess) {}
}
} // namespace Core
