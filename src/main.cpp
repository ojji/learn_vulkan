#include "core/Application.h"
#include "core/CopyToLocalBufferJob.h"
#include "core/CopyToLocalImageJob.h"
#include "core/Mat4.h"
#include "core/Transition.h"
#include "core/VulkanFunctions.h"
#include "core/VulkanRenderer.h"
#include "os/Common.h"
#include "os/Window.h"
#include "utils/ConsoleLogger.h"
#include "utils/FileLogger.h"
#include "utils/Logger.h"
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

class SampleApp : public Core::Application
{
public:
  Core::Mat4 GetUniformData()
  {
    vk::Extent2D currentExtent = Renderer()->GetSwapchainExtent();
    float halfWidth = static_cast<float>(currentExtent.width) / 2.0f;
    float halfHeight = static_cast<float>(currentExtent.height) / 2.0f;
    return Core::Mat4::GetOrthographic(-halfWidth, halfWidth, -halfHeight, halfHeight, -1.0f, 1.0f);
  }

  SampleApp() : Core::Application(), m_Transition(Core::Transition())
  {
    std::filesystem::path debugLog = Os::GetExecutableDirectory() / "logs/everything.log";
    std::filesystem::path keyboardLog = Os::GetExecutableDirectory() / "logs/keyboard.log";
    std::filesystem::path rendererLog = Os::GetExecutableDirectory() / "logs/renderer.log";

    Utils::Logger::Get().Register<Utils::ConsoleLogger>("ConsoleLogger");
    Utils::Logger::Get().Register<Utils::FileLogger>("DebugLogger", debugLog, Utils::FileLogger::OpenMode::Truncate);
    Utils::Logger::Get().Register<Utils::FileLogger>("KeyboardLogger",
                                                     keyboardLog,
                                                     Utils::FileLogger::OpenMode::Truncate,
                                                     std::initializer_list<std::string>{ std::string("Keyboard") });
    Utils::Logger::Get().Register<Utils::FileLogger>("RendererLogger",
                                                     rendererLog,
                                                     Utils::FileLogger::OpenMode::Truncate,
                                                     std::initializer_list<std::string>{ std::string("Renderer") });
    Utils::Logger::Get().MuteCategory("DebugLogger", "FrameStat");
    Utils::Logger::Get().MuteCategory("ConsoleLogger", "FrameStat");
  }

  virtual ~SampleApp() {}

  bool Initialize()
  {
    Application::Initialize(L"Hello Vulkan!", 1280, 720);
    GetWindow()->SetOnCharacterReceived([this](Os::Window* window, uint32_t codePoint, Core::ModifierKeys modifiers) {
      OnCharacterReceived(window, codePoint, modifiers);
    });

    GetWindow()->SetOnKeyEvent(
      [this](Os::Window* window,
             uint8_t keyCode,
             Core::KeypressAction action,
             Core::ModifierKeys modifiers,
             uint16_t repeatCount) { OnKeyEvent(window, keyCode, action, modifiers, repeatCount); });

    std::array<float, 4> color = { (85.0f / 255.0f), (87.0f / 255.0f), (112.0f / 255.0f), 0.0f };
    std::array<float, 4> otherColor = { (179.0f / 255.0f), (147.0f / 255.0f), (29.0f / 255.0f), 0.0f };
    m_Transition = Core::Transition(color, otherColor, 1.0f);

    QueryPerformanceCounter(&m_StartTime);
    QueryPerformanceFrequency(&m_Frequency);

    return true;
  }


  void OnCharacterReceived(Os::Window* window, uint32_t codePoint, Core::ModifierKeys modifiers)
  {
    (void)window;
    (void)codePoint;
    (void)modifiers;
    std::ostringstream debugMessage;
    debugMessage << "Character received: " << std::showbase << std::hex << codePoint
                 << ", modifiers: " << Core::enum_to_string(modifiers);
    Utils::Logger::Get().LogDebug(debugMessage.str(), "Keyboard");
  }

  void OnKeyEvent(Os::Window* window,
                  uint8_t keyCode,
                  Core::KeypressAction action,
                  Core::ModifierKeys modifiers,
                  uint16_t repeatCount)
  {
    (void)window;
    (void)keyCode;
    (void)action;
    (void)modifiers;
    (void)repeatCount;
    std::ostringstream debugMessage;
    debugMessage << "Keycode " << std::showbase << std::hex << static_cast<int>(keyCode) << " " << Core::enum_to_string(action)
                 << ", modifiers: " << Core::enum_to_string(modifiers) << ", repeatCount: " << std::dec << repeatCount;
    Utils::Logger::Get().LogDebug(debugMessage.str(), "Keyboard");
  }

  void InitializeRenderer() override
  {
    m_Vertices = {
      Core::VertexData{ { -256.0f, 256.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },  // bottom left
      Core::VertexData{ { 256.0f, 256.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },   // bottom right
      Core::VertexData{ { -256.0f, -256.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } }, // top left
      Core::VertexData{ { 256.0f, -256.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } }   // top right
    };

    m_VertexBuffer =
      Renderer()->CreateBuffer(static_cast<uint32_t>(m_Vertices.size()) * sizeof(Core::VertexData),
                               { vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer },
                               { vk::MemoryPropertyFlagBits::eDeviceLocal });

    // create sampler
    auto samplerCreateInfo = vk::SamplerCreateInfo(
      {},                                   // vk::SamplerCreateFlags flags_ = {},
      vk::Filter::eLinear,                  // vk::Filter magFilter_ = vk::Filter::eNearest,
      vk::Filter::eLinear,                  // vk::Filter minFilter_ = vk::Filter::eNearest,
      vk::SamplerMipmapMode::eNearest,      // vk::SamplerMipmapMode mipmapMode_ = vk::SamplerMipmapMode::eNearest,
      vk::SamplerAddressMode::eClampToEdge, // vk::SamplerAddressMode addressModeU_ = vk::SamplerAddressMode::eRepeat,
      vk::SamplerAddressMode::eClampToEdge, // vk::SamplerAddressMode addressModeV_ = vk::SamplerAddressMode::eRepeat,
      vk::SamplerAddressMode::eClampToEdge, // vk::SamplerAddressMode addressModeW_ = vk::SamplerAddressMode::eRepeat,
      0.0f,                                 // float mipLodBias_ = {},
      VK_FALSE,                             // vk::Bool32 anisotropyEnable_ = {},
      1.0f,                                 // float maxAnisotropy_ = {},
      VK_FALSE,                             // vk::Bool32 compareEnable_ = {},
      vk::CompareOp::eAlways,               // vk::CompareOp compareOp_ = vk::CompareOp::eNever,
      0.0f,                                 // float minLod_ = {},
      0.0f,                                 // float maxLod_ = {},
      vk::BorderColor::eFloatTransparentBlack, // vk::BorderColor borderColor_ =
                                               // vk::BorderColor::eFloatTransparentBlack,
      VK_FALSE                                 // vk::Bool32 unnormalizedCoordinates_ = {}
    );
    m_Sampler = Renderer()->GetDevice().createSampler(samplerCreateInfo);

    auto transferJob = std::shared_ptr<Core::CopyToLocalJob>(
      new Core::CopyToLocalBufferJob(Renderer(),
                                     m_Vertices.data(),
                                     static_cast<uint32_t>(m_Vertices.size() * sizeof(Core::VertexData)),
                                     m_VertexBuffer.m_Handle,
                                     vk::DeviceSize(0),
                                     { vk::AccessFlagBits::eVertexAttributeRead },
                                     { vk::PipelineStageFlagBits::eVertexInput },
                                     nullptr));

    AddToTransferQueue(transferJob);
    transferJob->WaitComplete();

    // read texture data
    uint32_t textureWidth, textureHeight;
    std::vector<char> textureData = Os::LoadTextureData("assets/Avatar_cat.png", textureWidth, textureHeight);

    m_Texture = Renderer()->CreateImage(textureWidth,
                                        textureHeight,
                                        { vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst },
                                        { vk::MemoryPropertyFlagBits::eDeviceLocal });

    auto textureCopyJob =
      std::shared_ptr<Core::CopyToLocalJob>(new Core::CopyToLocalImageJob(Renderer(),
                                                                          textureData.data(),
                                                                          textureData.size(),
                                                                          m_Texture.m_Width,
                                                                          m_Texture.m_Height,
                                                                          m_Texture.m_Handle,
                                                                          vk::ImageLayout::eShaderReadOnlyOptimal,
                                                                          vk::AccessFlagBits::eShaderRead,
                                                                          vk::PipelineStageFlagBits::eFragmentShader,
                                                                          nullptr));
    AddToTransferQueue(textureCopyJob);
    textureCopyJob->WaitComplete();

    auto imageInfo = vk::DescriptorImageInfo(
      m_Sampler,                              // vk::Sampler sampler_ = {},
      m_Texture.m_View,                       // vk::ImageView imageView_ = {},
      vk::ImageLayout::eShaderReadOnlyOptimal // vk::ImageLayout imageLayout_ = vk::ImageLayout::eUndefined
    );

    vk::WriteDescriptorSet imageAndSamplerDescriptorWrite =
      vk::WriteDescriptorSet(Renderer()->GetDescriptorSet(),            // vk::DescriptorSet dstSet_ = {},
                             0,                                         // uint32_t dstBinding_ = {},
                             0,                                         // uint32_t dstArrayElement_ = {},
                             1,                                         // uint32_t descriptorCount_ = {},
                             vk::DescriptorType::eCombinedImageSampler, // vk::DescriptorType descriptorType_ =
                                                                        // vk::DescriptorType::eSampler,
                             &imageInfo, // const vk::DescriptorImageInfo* pImageInfo_ = {},
                             nullptr,    // const vk::DescriptorBufferInfo* pBufferInfo_ = {},
                             nullptr     // const vk::BufferView* pTexelBufferView_ = {}
      );
    Renderer()->GetDevice().updateDescriptorSets(imageAndSamplerDescriptorWrite, nullptr);
  }

  void PreRender(Core::FrameResource const& frameResources) override
  {
    Core::Mat4 uniformData = GetUniformData();
    auto uniformTransfer = std::shared_ptr<Core::CopyToLocalBufferJob>(
      new Core::CopyToLocalBufferJob(Renderer(),
                                     reinterpret_cast<void*>(uniformData.GetData()),
                                     Core::Mat4::GetSize(),
                                     frameResources.m_UniformBuffer.m_Handle,
                                     vk::DeviceSize(0),
                                     { vk::AccessFlagBits::eShaderRead },
                                     { vk::PipelineStageFlagBits::eVertexShader },
                                     nullptr));

    AddToTransferQueue(uniformTransfer);
    uniformTransfer->WaitComplete();

    auto uniformBufferInfo =
      vk::DescriptorBufferInfo(frameResources.m_UniformBuffer.m_Handle, // vk::Buffer buffer_ = {},
                               vk::DeviceSize(0),                       // vk::DeviceSize offset_ = {},
                               Core::Mat4::GetSize()                    // vk::DeviceSize range_ = {}
      );

    auto uniformBufferDescriptorWrite = vk::WriteDescriptorSet(
      Renderer()->GetDescriptorSet(),     // vk::DescriptorSet dstSet_ = {},
      1,                                  // uint32_t dstBinding_ = {},
      0,                                  // uint32_t dstArrayElement_ = {},
      1,                                  // uint32_t descriptorCount_ = {},
      vk::DescriptorType::eUniformBuffer, // vk::DescriptorType descriptorType_ = vk::DescriptorType::eSampler,
      nullptr,                            // const vk::DescriptorImageInfo* pImageInfo_ = {},
      &uniformBufferInfo,                 // const vk::DescriptorBufferInfo* pBufferInfo_ = {},
      nullptr                             // const vk::BufferView* pTexelBufferView_ = {}
    );
    Renderer()->GetDevice().updateDescriptorSets(uniformBufferDescriptorWrite, nullptr);
  }

  void Render(Core::FrameResource const& frameResources, vk::CommandBuffer const& commandBuffer) override
  {
    LARGE_INTEGER currentTime, elapsedTimeInMilliSeconds;
    QueryPerformanceCounter(&currentTime);
    elapsedTimeInMilliSeconds.QuadPart = currentTime.QuadPart - m_StartTime.QuadPart;
    elapsedTimeInMilliSeconds.QuadPart *= 1000;
    elapsedTimeInMilliSeconds.QuadPart /= m_Frequency.QuadPart;

    vk::ClearValue clearValue = m_Transition.GetValue(static_cast<float>(elapsedTimeInMilliSeconds.QuadPart));
    auto renderPassBeginInfo = vk::RenderPassBeginInfo(
      Renderer()->GetRenderPass(),  // vk::RenderPass renderPass_ = {},
      frameResources.m_Framebuffer, // vk::Framebuffer framebuffer_ = {},
      vk::Rect2D(vk::Offset2D(0, 0),
                 vk::Extent2D(frameResources.m_SwapchainImage.m_ImageWidth,
                              frameResources.m_SwapchainImage.m_ImageHeight)), // vk::Rect2D renderArea_ = {},
      1,                                                                       // uint32_t clearValueCount_ = {},
      &clearValue // const vk::ClearValue* pClearValues_ = {}
    );

    commandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, Renderer()->GetPipeline());
    commandBuffer.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics, Renderer()->GetPipelineLayout(), 0, Renderer()->GetDescriptorSet(), nullptr);

    auto viewport =
      vk::Viewport(0.0f,                                                              // float x_ = {},
                   0.0f,                                                              // float y_ = {},
                   static_cast<float>(frameResources.m_SwapchainImage.m_ImageWidth),  // float width_ = {},
                   static_cast<float>(frameResources.m_SwapchainImage.m_ImageHeight), // float height_ = {},
                   0.0f,                                                              // float minDepth_ = {},
                   1.0f                                                               // float maxDepth_ = {}
      );

    commandBuffer.setViewport(0, viewport);

    auto scissor = vk::Rect2D(
      vk::Offset2D(0, 0),
      vk::Extent2D(frameResources.m_SwapchainImage.m_ImageWidth, frameResources.m_SwapchainImage.m_ImageHeight));
    commandBuffer.setScissor(0, scissor);
    commandBuffer.bindVertexBuffers(0, m_VertexBuffer.m_Handle, vk::DeviceSize(0));
    commandBuffer.draw(static_cast<uint32_t>(m_Vertices.size()), 1, 0, 0);
    commandBuffer.endRenderPass();
  }

  void PostRender(Core::FrameStat const& frameStats) override
  {
    double frameTimeInMs = Renderer()->GetFrameTimeInMs(frameStats);
    double fps = 1.0 / (frameTimeInMs / 1'000);
    std::ostringstream fpsMessage;
    fpsMessage << "GPU time: " << frameTimeInMs << " ms (" << fps << " fps)";
    Utils::Logger::Get().LogDebug(fpsMessage.str(), "FrameStat");
  }

  void OnDestroyRenderer()
  {
    Renderer()->GetDevice().destroySampler(m_Sampler);
    {
      Renderer()->GetDevice().destroyImageView(m_Texture.m_View);
      m_Texture.m_View = nullptr;
      Renderer()->GetDevice().freeMemory(m_Texture.m_Memory);
      m_Texture.m_Memory = nullptr;
      Renderer()->GetDevice().destroyImage(m_Texture.m_Handle);
      m_Texture.m_Handle = nullptr;
      m_Texture.m_Width = 0;
      m_Texture.m_Height = 0;
    }
    Renderer()->FreeBuffer(m_VertexBuffer);
  }

private:
  Core::Transition m_Transition;
  LARGE_INTEGER m_StartTime;
  LARGE_INTEGER m_Frequency;

  std::vector<Core::VertexData> m_Vertices;
  Core::BufferData m_VertexBuffer;
  vk::Sampler m_Sampler;
  Core::ImageData m_Texture;
};

int main()
{
  SampleApp app;

  if (!app.Initialize()) { return 1; }

  if (!app.Start()) { return 1; }

  return 0;
}