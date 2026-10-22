#pragma once
#include "rhi.h"
#include "vulkanContext.h"
#include "vulkanRenderGraph.h"
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

struct SwapchainConfiguration;

class VulkanPresenter {
public:
    VulkanPresenter() = default;
    ~VulkanPresenter() noexcept;
    VulkanPresenter(const VulkanPresenter&) = delete;
    VulkanPresenter& operator=(const VulkanPresenter&) = delete;
    void initialize(GLFWwindow* windowHandle, const VulkanDeviceView& deviceView, const CompiledRenderGraph& plan);
    void renderFrame();
    void shutdown();
private:
    struct FrameResources {
        VkCommandPool commandPoolHandle = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> groupCommandBufferHandles;
        VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
        VkFence submissionFence = VK_NULL_HANDLE;
    };
    struct ImageResources {
        VkImage imageHandle = VK_NULL_HANDLE; // Borrowed from swapchain.
        VkImageView imageViewHandle = VK_NULL_HANDLE;
        VkFramebuffer framebufferHandle = VK_NULL_HANDLE;
        VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    };
    struct SwapchainResources {
        explicit SwapchainResources(VkDevice deviceHandle, std::uint32_t generation);
        ~SwapchainResources() noexcept;
        SwapchainResources(const SwapchainResources&) = delete;
        SwapchainResources& operator=(const SwapchainResources&) = delete;
        VkDevice deviceHandle;
        std::uint32_t generation;
        VkSwapchainKHR swapchainHandle = VK_NULL_HANDLE;
        std::array<VkRenderPass, 4> renderPassHandles{}; // Only load/store variants used by the plan are created.
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D renderExtent{};
        VkExtent2D framebufferExtent{};
        std::vector<ImageResources> images;
        bool retired = false;
    };
    struct WindowState {
        VkExtent2D framebufferExtent{};
        bool iconified = false;
        bool closing = false;
    };
    WindowState readWindowState(const char* stage) const;
    void setPresentationPaused(bool paused, const char* reason);
    void createFrameResources();
    std::unique_ptr<SwapchainResources> createSwapchain(const SwapchainConfiguration& configuration,
        VkExtent2D framebufferExtent, VkSwapchainKHR oldSwapchainHandle);
    void createRenderPass(SwapchainResources& resources, VkFormat colorFormat);
    std::string recordGraphFrame(const FrameResources& frame, const PreparedGraphFrame& prepared, bool captureTrace);
    static std::string recordRenderGroup(const PreparedRenderGroup& group, VkCommandBuffer commandBufferHandle, bool captureTrace);
    std::string submitGraphFrame(const FrameResources& frame, const PreparedGraphFrame& prepared, VkSemaphore renderFinishedSemaphore, bool captureTrace);
    bool rebuildSwapchain();
    VkResult waitForSubmissions() noexcept;
    static void releaseSwapchain(SwapchainResources& resources) noexcept;
    void releaseResources() noexcept;
    GLFWwindow* windowHandle = nullptr; // Borrowed; outlives this presenter.
    VulkanDeviceView deviceView{};
    const CompiledRenderGraph* graphPlan = nullptr; // Backend-owned; outlives this presenter.
    std::array<FrameResources, 2> frames{};
    std::unique_ptr<SwapchainResources> swapchainResources;
    std::uint32_t frameIndex = 0;
    std::uint64_t submittedFrames = 0;
    std::uint64_t presentedFrames = 0;
    std::uint32_t swapchainGeneration = 0;
    bool rebuildRequested = false;
    bool hasSubmittedWork = false;
    bool presentationPaused = false;
    std::uint32_t reportedGeneration = 0;
    std::uint64_t reportedPlanVersion = 0;
#ifdef ZL_RENDERER_TESTING
    bool testInjectionUsed = false;
#endif
};
