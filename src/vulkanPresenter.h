#pragma once
#include "rhi.h"
#include "vulkanContext.h"
#include <array>
#include <cstdint>
#include <vector>

class VulkanPresenter {
public:
    VulkanPresenter() = default;
    ~VulkanPresenter() noexcept;
    VulkanPresenter(const VulkanPresenter&) = delete;
    VulkanPresenter& operator=(const VulkanPresenter&) = delete;
    void initialize(GLFWwindow* windowHandle, const VulkanDeviceView& deviceView, ClearColor clearColor);
    void renderFrame();
    void shutdown();
private:
    struct FrameResources {
        VkCommandPool commandPoolHandle = VK_NULL_HANDLE;
        VkCommandBuffer commandBufferHandle = VK_NULL_HANDLE;
        VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
        VkFence submissionFence = VK_NULL_HANDLE;
    };
    struct ImageResources {
        VkImage imageHandle = VK_NULL_HANDLE; // Borrowed from swapchain.
        VkImageView imageViewHandle = VK_NULL_HANDLE;
        VkFramebuffer framebufferHandle = VK_NULL_HANDLE;
        VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    };
    void createFrameResources();
    void createSwapchain(VkExtent2D framebufferExtent);
    void createRenderPass(VkFormat colorFormat);
    void recordClearFrame(const FrameResources& frame, const ImageResources& image);
    void rebuildSwapchain(VkExtent2D framebufferExtent);
    VkResult waitForSubmissions() noexcept;
    void releaseSwapchain() noexcept;
    void releaseResources() noexcept;
    GLFWwindow* windowHandle = nullptr; // Borrowed; outlives this presenter.
    VulkanDeviceView deviceView{};
    ClearColor clearColor{};
    std::array<FrameResources, 2> frames{};
    std::vector<ImageResources> images;
    VkSwapchainKHR swapchainHandle = VK_NULL_HANDLE;
    VkRenderPass renderPassHandle = VK_NULL_HANDLE;
    VkExtent2D renderExtent{};
    VkExtent2D lastFramebufferExtent{};
    std::uint32_t frameIndex = 0;
    std::uint64_t submittedFrames = 0;
    std::uint64_t presentedFrames = 0;
    std::uint32_t swapchainGeneration = 0;
    bool rebuildRequested = false;
    bool hasSubmittedWork = false;
#ifdef ZL_RENDERER_TESTING
    bool testInjectionUsed = false;
#endif
};
