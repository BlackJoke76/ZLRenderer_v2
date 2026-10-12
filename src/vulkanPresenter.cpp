#include "vulkanPresenter.h"
#include "swapchainSelection.h"
#include "vulkanUtilities.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cstdio>
#include <exception>
#ifdef ZL_RENDERER_TESTING
#include "vulkanTestHooks.h"
#endif

namespace {
constexpr std::uint64_t frameWaitNanoseconds = 100'000'000;
VkSemaphore createSemaphore(VkDevice deviceHandle) {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore semaphoreHandle = VK_NULL_HANDLE;
    checkResult(vkCreateSemaphore(deviceHandle, &semaphoreInfo, nullptr, &semaphoreHandle), "sync: vkCreateSemaphore");
    return semaphoreHandle;
}
}

VulkanPresenter::~VulkanPresenter() noexcept {
    try { shutdown(); }
    catch (const std::exception& error) { std::fprintf(stderr, "Cleanup failed: %s\n", error.what()); }
}

void VulkanPresenter::initialize(GLFWwindow* requestedWindowHandle, const VulkanDeviceView& requestedDeviceView,
                                 ClearColor requestedClearColor) {
    windowHandle = requestedWindowHandle;
    deviceView = requestedDeviceView;
    clearColor = requestedClearColor;
    createFrameResources();
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(windowHandle, &framebufferWidth, &framebufferHeight);
    if (framebufferWidth > 0 && framebufferHeight > 0 && glfwGetWindowAttrib(windowHandle, GLFW_ICONIFIED) == GLFW_FALSE) {
        createSwapchain({static_cast<std::uint32_t>(framebufferWidth), static_cast<std::uint32_t>(framebufferHeight)});
    }
    std::printf("RHI initialization succeeded; clearColor=(%.3f,%.3f,%.3f,%.3f); frameSlots=%zu\n",
                clearColor.red, clearColor.green, clearColor.blue, clearColor.alpha, frames.size());
}

void VulkanPresenter::createFrameResources() {
    for (auto& frame : frames) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = deviceView.queueFamilies.graphicsFamilyIndex;
        VkCommandPool createdPoolHandle = VK_NULL_HANDLE;
        checkResult(vkCreateCommandPool(deviceView.deviceHandle, &poolInfo, nullptr, &createdPoolHandle),
                    "frames: vkCreateCommandPool");
        frame.commandPoolHandle = createdPoolHandle;
        std::puts("Created command pool");
        VkCommandBufferAllocateInfo allocationInfo{};
        allocationInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocationInfo.commandPool = frame.commandPoolHandle;
        allocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocationInfo.commandBufferCount = 1;
        VkCommandBuffer createdBufferHandle = VK_NULL_HANDLE;
        checkResult(vkAllocateCommandBuffers(deviceView.deviceHandle, &allocationInfo, &createdBufferHandle),
                    "frames: vkAllocateCommandBuffers");
        frame.commandBufferHandle = createdBufferHandle;
        frame.imageAvailableSemaphore = createSemaphore(deviceView.deviceHandle);
        std::puts("Created acquire semaphore");
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VkFence createdFenceHandle = VK_NULL_HANDLE;
        checkResult(vkCreateFence(deviceView.deviceHandle, &fenceInfo, nullptr, &createdFenceHandle), "frames: vkCreateFence");
        frame.submissionFence = createdFenceHandle;
        std::puts("Created submission fence");
#ifdef ZL_RENDERER_TESTING
        runVulkanTestCheckpoint("frame-resources", VK_NULL_HANDLE);
#endif
    }
}

void VulkanPresenter::createRenderPass(VkFormat colorFormat) {
    VkAttachmentDescription attachment{};
    attachment.format = colorFormat;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    const VkAttachmentReference colorReference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    passInfo.attachmentCount = 1;
    passInfo.pAttachments = &attachment;
    passInfo.subpassCount = 1;
    passInfo.pSubpasses = &subpass;
    passInfo.dependencyCount = 1;
    passInfo.pDependencies = &dependency;
    VkRenderPass createdPassHandle = VK_NULL_HANDLE;
    checkResult(vkCreateRenderPass(deviceView.deviceHandle, &passInfo, nullptr, &createdPassHandle), "swapchain: vkCreateRenderPass");
    renderPassHandle = createdPassHandle;
    std::puts("Created render pass");
}

void VulkanPresenter::createSwapchain(VkExtent2D framebufferExtent) {
    auto support = querySurfaceSupport(deviceView.physicalDeviceHandle, deviceView.surfaceHandle);
#ifdef ZL_RENDERER_TESTING
    if (isVulkanTestCase("fifo") || isVulkanTestCase("fifo-window")) {
        // FIFO is mandatory for ordinary surfaces; still select from the real supported list.
        std::erase(support.presentModes, VK_PRESENT_MODE_MAILBOX_KHR);
    }
#endif
    const auto configuration = selectSwapchainConfiguration(support, framebufferExtent, deviceView.queueFamilies);
    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = deviceView.surfaceHandle;
    swapchainInfo.minImageCount = configuration.imageCount;
    swapchainInfo.imageFormat = configuration.surfaceFormat.format;
    swapchainInfo.imageColorSpace = configuration.surfaceFormat.colorSpace;
    swapchainInfo.imageExtent = configuration.extent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.imageSharingMode = configuration.sharingMode;
    if (configuration.sharingMode == VK_SHARING_MODE_CONCURRENT) {
        swapchainInfo.queueFamilyIndexCount = static_cast<std::uint32_t>(configuration.familyIndices.size());
        swapchainInfo.pQueueFamilyIndices = configuration.familyIndices.data();
    }
    swapchainInfo.preTransform = configuration.transform;
    swapchainInfo.compositeAlpha = configuration.compositeAlpha;
    swapchainInfo.presentMode = configuration.presentMode;
    swapchainInfo.clipped = VK_TRUE;
    VkSwapchainKHR createdSwapchainHandle = VK_NULL_HANDLE;
    checkResult(vkCreateSwapchainKHR(deviceView.deviceHandle, &swapchainInfo, nullptr, &createdSwapchainHandle),
                "swapchain: vkCreateSwapchainKHR");
    swapchainHandle = createdSwapchainHandle;
    ++swapchainGeneration;
    renderExtent = configuration.extent;
    lastFramebufferExtent = framebufferExtent;
    std::puts("Created swapchain");
#ifdef ZL_RENDERER_TESTING
    runVulkanTestCheckpoint("swapchain", VK_NULL_HANDLE);
    if (swapchainGeneration > 1) { runVulkanTestCheckpoint("rebuild-failure", VK_NULL_HANDLE); }
#endif
    const auto imageHandles = enumerateProperties<VkImage>([this](auto* count, auto* handles) {
        return vkGetSwapchainImagesKHR(deviceView.deviceHandle, swapchainHandle, count, handles);
    }, "swapchain: vkGetSwapchainImagesKHR");
    if (imageHandles.empty()) { throw std::runtime_error("swapchain: no images returned."); }
    images.resize(imageHandles.size());
    createRenderPass(configuration.surfaceFormat.format);
    for (std::size_t imageIndex = 0; imageIndex < images.size(); ++imageIndex) {
        auto& image = images[imageIndex];
        image.imageHandle = imageHandles[imageIndex];
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image.imageHandle;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = configuration.surfaceFormat.format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView createdViewHandle = VK_NULL_HANDLE;
        checkResult(vkCreateImageView(deviceView.deviceHandle, &viewInfo, nullptr, &createdViewHandle), "swapchain: vkCreateImageView");
        image.imageViewHandle = createdViewHandle;
        std::puts("Created image view");
#ifdef ZL_RENDERER_TESTING
        runVulkanTestCheckpoint("image-view", VK_NULL_HANDLE);
#endif
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPassHandle;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &image.imageViewHandle;
        framebufferInfo.width = renderExtent.width;
        framebufferInfo.height = renderExtent.height;
        framebufferInfo.layers = 1;
        VkFramebuffer createdFramebufferHandle = VK_NULL_HANDLE;
        checkResult(vkCreateFramebuffer(deviceView.deviceHandle, &framebufferInfo, nullptr, &createdFramebufferHandle),
                    "swapchain: vkCreateFramebuffer");
        image.framebufferHandle = createdFramebufferHandle;
        std::puts("Created framebuffer");
#ifdef ZL_RENDERER_TESTING
        runVulkanTestCheckpoint("framebuffer", VK_NULL_HANDLE);
#endif
        image.renderFinishedSemaphore = createSemaphore(deviceView.deviceHandle);
        std::puts("Created present semaphore");
    }
    rebuildRequested = false;
    std::printf("Swapchain generation=%u mode=%s format=%d colorSpace=%d extent=%ux%u images=%zu sharing=%s%s\n",
                swapchainGeneration, configuration.presentMode == VK_PRESENT_MODE_MAILBOX_KHR ? "MAILBOX" : "FIFO",
                configuration.surfaceFormat.format, configuration.surfaceFormat.colorSpace,
                renderExtent.width, renderExtent.height, images.size(),
                configuration.sharingMode == VK_SHARING_MODE_EXCLUSIVE ? "EXCLUSIVE" : "CONCURRENT",
                configuration.presentMode == VK_PRESENT_MODE_FIFO_KHR ? " (MAILBOX unavailable in selection)" : "");
}

void VulkanPresenter::recordClearFrame(const FrameResources& frame, const ImageResources& image) {
    checkResult(vkResetCommandPool(deviceView.deviceHandle, frame.commandPoolHandle, 0), "record: vkResetCommandPool");
#ifdef ZL_RENDERER_TESTING
    runVulkanTestCheckpoint("record", VK_NULL_HANDLE);
#endif
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkResult(vkBeginCommandBuffer(frame.commandBufferHandle, &beginInfo), "record: vkBeginCommandBuffer");
    const VkClearValue clearValue{{{clearColor.red, clearColor.green, clearColor.blue, clearColor.alpha}}};
    VkRenderPassBeginInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passInfo.renderPass = renderPassHandle;
    passInfo.framebuffer = image.framebufferHandle;
    passInfo.renderArea.extent = renderExtent;
    passInfo.clearValueCount = 1;
    passInfo.pClearValues = &clearValue;
    vkCmdBeginRenderPass(frame.commandBufferHandle, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(frame.commandBufferHandle);
    checkResult(vkEndCommandBuffer(frame.commandBufferHandle), "record: vkEndCommandBuffer");
}

void VulkanPresenter::renderFrame() {
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(windowHandle, &framebufferWidth, &framebufferHeight);
    if (framebufferWidth <= 0 || framebufferHeight <= 0 || glfwGetWindowAttrib(windowHandle, GLFW_ICONIFIED) == GLFW_TRUE) { return; }
    const VkExtent2D framebufferExtent{static_cast<std::uint32_t>(framebufferWidth), static_cast<std::uint32_t>(framebufferHeight)};
#ifdef ZL_RENDERER_TESTING
    if (isVulkanTestCase("rebuild-failure") && submittedFrames == 3) { rebuildRequested = true; }
#endif
    if (!swapchainHandle || rebuildRequested || framebufferExtent.width != lastFramebufferExtent.width ||
        framebufferExtent.height != lastFramebufferExtent.height) { rebuildSwapchain(framebufferExtent); }
    auto& frame = frames[frameIndex];
    auto waitResult = vkWaitForFences(deviceView.deviceHandle, 1, &frame.submissionFence, VK_TRUE, frameWaitNanoseconds);
#ifdef ZL_RENDERER_TESTING
    if (!testInjectionUsed && isVulkanTestCase("fence-timeout")) { testInjectionUsed = true; waitResult = VK_TIMEOUT; }
#endif
    if (waitResult == VK_TIMEOUT) { std::puts("Frame skipped: fence timeout"); return; }
    checkResult(waitResult, "frame wait: vkWaitForFences");
    std::uint32_t imageIndex = 0;
    VkResult acquireResult;
#ifdef ZL_RENDERER_TESTING
    if (!testInjectionUsed && (isVulkanTestCase("acquire-out-of-date") || isVulkanTestCase("acquire-timeout") ||
                               isVulkanTestCase("surface-lost"))) {
        testInjectionUsed = true;
        acquireResult = isVulkanTestCase("surface-lost") ? VK_ERROR_SURFACE_LOST_KHR :
                        isVulkanTestCase("acquire-timeout") ? VK_TIMEOUT : VK_ERROR_OUT_OF_DATE_KHR;
        std::printf("Injected acquire result=%d before actual acquisition\n", acquireResult);
    } else
#endif
    { acquireResult = vkAcquireNextImageKHR(deviceView.deviceHandle, swapchainHandle, frameWaitNanoseconds,
                                            frame.imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex); }
#ifdef ZL_RENDERER_TESTING
    if (acquireResult == VK_SUCCESS && !testInjectionUsed && isVulkanTestCase("acquire-suboptimal")) {
        testInjectionUsed = true;
        acquireResult = VK_SUBOPTIMAL_KHR; // Real acquired image/semaphore is consumed below.
        std::puts("Injected SUBOPTIMAL after real acquisition");
    }
#endif
    if (acquireResult == VK_TIMEOUT || acquireResult == VK_NOT_READY) { std::puts("Frame skipped: acquire timeout/not ready"); return; }
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) { rebuildRequested = true; std::puts("Rebuild requested: acquire OUT_OF_DATE"); return; }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        checkResult(acquireResult, "acquire: vkAcquireNextImageKHR");
    }
    if (acquireResult == VK_SUBOPTIMAL_KHR) { rebuildRequested = true; std::puts("Rebuild requested: acquire SUBOPTIMAL"); }
    const auto& image = images.at(imageIndex);
    recordClearFrame(frame, image);
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &frame.imageAvailableSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.commandBufferHandle;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &image.renderFinishedSemaphore;
    checkResult(vkResetFences(deviceView.deviceHandle, 1, &frame.submissionFence), "submit: vkResetFences");
    VkResult submitResult;
#ifdef ZL_RENDERER_TESTING
    if (isVulkanTestCase("submit")) { submitResult = VK_ERROR_INITIALIZATION_FAILED; }
    else
#endif
    { submitResult = vkQueueSubmit(deviceView.graphicsQueueHandle, 1, &submitInfo, frame.submissionFence); }
    checkResult(submitResult, "submit: vkQueueSubmit");
    hasSubmittedWork = true;
    ++submittedFrames;
#ifdef ZL_RENDERER_TESTING
    if (submittedFrames <= 12) {
        std::printf("Frame submitted slot=%u image=%u command=%p acquire=%p present=%p fence=%p generation=%u\n", frameIndex, imageIndex,
                    static_cast<void*>(frame.commandBufferHandle), reinterpret_cast<void*>(frame.imageAvailableSemaphore),
                    reinterpret_cast<void*>(image.renderFinishedSemaphore), reinterpret_cast<void*>(frame.submissionFence), swapchainGeneration);
    }
#endif
    frameIndex = (frameIndex + 1) % static_cast<std::uint32_t>(frames.size());
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &image.renderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchainHandle;
    presentInfo.pImageIndices = &imageIndex;
    auto presentResult = vkQueuePresentKHR(deviceView.presentQueueHandle, &presentInfo);
#ifdef ZL_RENDERER_TESTING
    if (presentResult == VK_SUCCESS && !testInjectionUsed &&
        (isVulkanTestCase("present-out-of-date") || isVulkanTestCase("present-suboptimal") || isVulkanTestCase("present-failure"))) {
        testInjectionUsed = true;
        // Actual present was enqueued; failure injection changes only subsequent host control flow.
        presentResult = isVulkanTestCase("present-failure") ? VK_ERROR_SURFACE_LOST_KHR :
                        isVulkanTestCase("present-suboptimal") ? VK_SUBOPTIMAL_KHR : VK_ERROR_OUT_OF_DATE_KHR;
        std::printf("Injected host present result=%d after real presentation\n", presentResult);
    }
#endif
    if (presentResult == VK_SUCCESS || presentResult == VK_SUBOPTIMAL_KHR) { ++presentedFrames; }
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        rebuildRequested = true;
        std::printf("Rebuild requested: present result=%d\n", presentResult);
    } else { checkResult(presentResult, "present: vkQueuePresentKHR"); }
#ifdef ZL_RENDERER_TESTING
    if (submittedFrames >= 12 && !isVulkanTestCase("alternate-window") && !isVulkanTestCase("fifo-window")) {
        glfwSetWindowShouldClose(windowHandle, GLFW_TRUE);
    }
#endif
}

VkResult VulkanPresenter::waitForSubmissions() noexcept {
    if (!hasSubmittedWork) { return VK_SUCCESS; }
    const auto waitResult = vkDeviceWaitIdle(deviceView.deviceHandle);
    hasSubmittedWork = false; // No further submission is permitted if the wait fails.
    std::printf("GPU completion wait result=%d\n", waitResult);
    return waitResult;
}

void VulkanPresenter::rebuildSwapchain(VkExtent2D framebufferExtent) {
    std::puts("Rebuilding swapchain");
    checkResult(waitForSubmissions(), "rebuild: vkDeviceWaitIdle");
    for (const auto& frame : frames) {
        checkResult(vkResetCommandPool(deviceView.deviceHandle, frame.commandPoolHandle, 0), "rebuild: vkResetCommandPool");
    }
    releaseSwapchain();
    createSwapchain(framebufferExtent);
}

void VulkanPresenter::releaseSwapchain() noexcept {
    for (auto& image : images) {
        if (image.framebufferHandle) {
            vkDestroyFramebuffer(deviceView.deviceHandle, image.framebufferHandle, nullptr);
            std::puts("Released framebuffer");
        }
    }
    for (auto& image : images) {
        if (image.imageViewHandle) {
            vkDestroyImageView(deviceView.deviceHandle, image.imageViewHandle, nullptr);
            std::puts("Released image view");
        }
    }
    if (renderPassHandle) {
        vkDestroyRenderPass(deviceView.deviceHandle, renderPassHandle, nullptr);
        renderPassHandle = VK_NULL_HANDLE;
        std::puts("Released render pass");
    }
    for (auto& image : images) {
        if (image.renderFinishedSemaphore) {
            vkDestroySemaphore(deviceView.deviceHandle, image.renderFinishedSemaphore, nullptr);
            std::puts("Released present semaphore");
        }
    }
    images.clear();
    if (swapchainHandle) {
        // Base WSI WaitIdle cleanup limitation is documented in ARCHITECTURE.md.
        vkDestroySwapchainKHR(deviceView.deviceHandle, swapchainHandle, nullptr);
        swapchainHandle = VK_NULL_HANDLE;
        std::puts("Released swapchain");
    }
}

void VulkanPresenter::releaseResources() noexcept {
    for (auto& frame : frames) {
        if (frame.commandPoolHandle) {
            vkDestroyCommandPool(deviceView.deviceHandle, frame.commandPoolHandle, nullptr);
            frame.commandPoolHandle = VK_NULL_HANDLE;
            frame.commandBufferHandle = VK_NULL_HANDLE;
            std::puts("Released command pool");
        }
    }
    releaseSwapchain();
    for (auto& frame : frames) {
        if (frame.imageAvailableSemaphore) {
            vkDestroySemaphore(deviceView.deviceHandle, frame.imageAvailableSemaphore, nullptr);
            frame.imageAvailableSemaphore = VK_NULL_HANDLE;
            std::puts("Released acquire semaphore");
        }
        if (frame.submissionFence) {
            vkDestroyFence(deviceView.deviceHandle, frame.submissionFence, nullptr);
            frame.submissionFence = VK_NULL_HANDLE;
            std::puts("Released submission fence");
        }
    }
}

void VulkanPresenter::shutdown() {
    if (!deviceView.deviceHandle) { return; }
    auto waitResult = waitForSubmissions();
#ifdef ZL_RENDERER_TESTING
    if (isVulkanTestCase("wait-failure") && submittedFrames > 0 && waitResult == VK_SUCCESS) {
        waitResult = VK_ERROR_DEVICE_LOST; // Inject only after the real GPU wait completed.
        std::puts("Injected host wait failure after actual idle");
    }
#endif
    releaseResources();
    deviceView = {};
    windowHandle = nullptr;
    std::printf("Frame summary: submitted=%llu presented=%llu generations=%u\n",
                static_cast<unsigned long long>(submittedFrames), static_cast<unsigned long long>(presentedFrames), swapchainGeneration);
    checkResult(waitResult, "shutdown: vkDeviceWaitIdle");
}
