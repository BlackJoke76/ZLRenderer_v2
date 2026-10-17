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
void reportSwapchainResource(const char* action, const char* resourceType, std::uint32_t generation, void* resourceHandle) noexcept {
    std::printf("%s %s\n", action, resourceType);
#ifdef ZL_RENDERER_TESTING
    std::printf("Generation resource action=%s generation=%u type=%s handle=%p\n", action, generation, resourceType, resourceHandle);
#else
    (void)generation;
    (void)resourceHandle;
#endif
}
VkSemaphore createSemaphore(VkDevice deviceHandle) {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore semaphoreHandle = VK_NULL_HANDLE;
    checkResult(vkCreateSemaphore(deviceHandle, &semaphoreInfo, nullptr, &semaphoreHandle), "sync: vkCreateSemaphore");
    return semaphoreHandle;
}
}

VulkanPresenter::SwapchainResources::SwapchainResources(VkDevice requestedDeviceHandle, std::uint32_t requestedGeneration)
    : deviceHandle(requestedDeviceHandle), generation(requestedGeneration) {}

VulkanPresenter::SwapchainResources::~SwapchainResources() noexcept {
    VulkanPresenter::releaseSwapchain(*this);
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
    const auto state = readWindowState("initialize");
    if (!state.closing && !state.iconified && state.framebufferExtent.width > 0 && state.framebufferExtent.height > 0) {
        (void)rebuildSwapchain();
    } else if (!state.closing) {
        setPresentationPaused(true, "minimized/zero framebuffer");
    }
    std::printf("RHI initialization succeeded; clearColor=(%.3f,%.3f,%.3f,%.3f); frameSlots=%zu\n",
                clearColor.red, clearColor.green, clearColor.blue, clearColor.alpha, frames.size());
}

VulkanPresenter::WindowState VulkanPresenter::readWindowState(const char* stage) const {
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(windowHandle, &framebufferWidth, &framebufferHeight);
    auto iconified = glfwGetWindowAttrib(windowHandle, GLFW_ICONIFIED) == GLFW_TRUE;
    auto closing = glfwWindowShouldClose(windowHandle) == GLFW_TRUE;
#ifdef ZL_RENDERER_TESTING
    applyWindowStateTest(stage, windowHandle, submittedFrames, framebufferWidth, framebufferHeight, iconified, closing);
#else
    (void)stage;
#endif
    return {{framebufferWidth > 0 ? static_cast<std::uint32_t>(framebufferWidth) : 0,
             framebufferHeight > 0 ? static_cast<std::uint32_t>(framebufferHeight) : 0}, iconified, closing};
}

void VulkanPresenter::setPresentationPaused(bool paused, const char* reason) {
    if (presentationPaused == paused) { return; }
    presentationPaused = paused;
    std::printf("Presentation %s: %s; generation=%u submitted=%llu presented=%llu rebuildRequested=%d\n",
                paused ? "paused" : "resumed", reason, swapchainGeneration,
                static_cast<unsigned long long>(submittedFrames), static_cast<unsigned long long>(presentedFrames),
                rebuildRequested ? 1 : 0);
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

void VulkanPresenter::createRenderPass(SwapchainResources& resources, VkFormat colorFormat) {
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
    resources.renderPassHandle = createdPassHandle;
    reportSwapchainResource("Created", "render pass", resources.generation, reinterpret_cast<void*>(resources.renderPassHandle));
#ifdef ZL_RENDERER_TESTING
    runSwapchainTestCheckpoint("render-pass", resources.generation);
#endif
}

std::unique_ptr<VulkanPresenter::SwapchainResources> VulkanPresenter::createSwapchain(
    const SwapchainConfiguration& configuration, VkExtent2D framebufferExtent, VkSwapchainKHR oldSwapchainHandle) {
    auto resources = std::make_unique<SwapchainResources>(deviceView.deviceHandle, swapchainGeneration + 1);
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
    swapchainInfo.oldSwapchain = oldSwapchainHandle;
#ifdef ZL_RENDERER_TESTING
    std::printf("Swapchain create generation=%u old=%p\n", resources->generation, reinterpret_cast<void*>(oldSwapchainHandle));
#endif
    if (swapchainResources) { swapchainResources->retired = true; }
    VkSwapchainKHR createdSwapchainHandle = VK_NULL_HANDLE;
    VkResult createResult;
#ifdef ZL_RENDERER_TESTING
    if (oldSwapchainHandle && isVulkanTestCase("rebuild-create")) {
        std::puts("Injected replacement create failure before actual call; host retirement control only");
        createResult = VK_ERROR_INITIALIZATION_FAILED;
    } else
#endif
    { createResult = vkCreateSwapchainKHR(deviceView.deviceHandle, &swapchainInfo, nullptr, &createdSwapchainHandle); }
    checkResult(createResult, "swapchain: vkCreateSwapchainKHR");
    resources->swapchainHandle = createdSwapchainHandle;

    resources->renderExtent = configuration.extent;
    resources->framebufferExtent = framebufferExtent;
    reportSwapchainResource("Created", "swapchain", resources->generation, reinterpret_cast<void*>(resources->swapchainHandle));
#ifdef ZL_RENDERER_TESTING
    std::printf("Swapchain created generation=%u handle=%p old=%p\n", resources->generation,
                reinterpret_cast<void*>(resources->swapchainHandle), reinterpret_cast<void*>(oldSwapchainHandle));
#endif
#ifdef ZL_RENDERER_TESTING
    runVulkanTestCheckpoint("swapchain", VK_NULL_HANDLE);
    if (resources->generation > 1) { runVulkanTestCheckpoint("rebuild-failure", VK_NULL_HANDLE); }
#endif
    const auto imageHandles = enumerateProperties<VkImage>([this, &resources](auto* count, auto* handles) {
        return vkGetSwapchainImagesKHR(deviceView.deviceHandle, resources->swapchainHandle, count, handles);
    }, "swapchain: vkGetSwapchainImagesKHR");
    if (imageHandles.empty()) { throw std::runtime_error("swapchain: no images returned."); }
    resources->images.resize(imageHandles.size());
    createRenderPass(*resources, configuration.surfaceFormat.format);
    for (std::size_t imageIndex = 0; imageIndex < resources->images.size(); ++imageIndex) {
        auto& image = resources->images[imageIndex];
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
        reportSwapchainResource("Created", "image view", resources->generation, reinterpret_cast<void*>(image.imageViewHandle));
#ifdef ZL_RENDERER_TESTING
        runSwapchainTestCheckpoint("image-view", resources->generation);
#endif
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = resources->renderPassHandle;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &image.imageViewHandle;
        framebufferInfo.width = resources->renderExtent.width;
        framebufferInfo.height = resources->renderExtent.height;
        framebufferInfo.layers = 1;
        VkFramebuffer createdFramebufferHandle = VK_NULL_HANDLE;
        checkResult(vkCreateFramebuffer(deviceView.deviceHandle, &framebufferInfo, nullptr, &createdFramebufferHandle),
                    "swapchain: vkCreateFramebuffer");
        image.framebufferHandle = createdFramebufferHandle;
        reportSwapchainResource("Created", "framebuffer", resources->generation, reinterpret_cast<void*>(image.framebufferHandle));
#ifdef ZL_RENDERER_TESTING
        runSwapchainTestCheckpoint("framebuffer", resources->generation);
#endif
        image.renderFinishedSemaphore = createSemaphore(deviceView.deviceHandle);
        reportSwapchainResource("Created", "present semaphore", resources->generation, reinterpret_cast<void*>(image.renderFinishedSemaphore));
#ifdef ZL_RENDERER_TESTING
        runSwapchainTestCheckpoint("present-semaphore", resources->generation);
#endif
    }
    std::printf("Swapchain generation=%u mode=%s format=%d colorSpace=%d extent=%ux%u images=%zu sharing=%s%s\n",
                resources->generation, configuration.presentMode == VK_PRESENT_MODE_MAILBOX_KHR ? "MAILBOX" : "FIFO",
                configuration.surfaceFormat.format, configuration.surfaceFormat.colorSpace,
                resources->renderExtent.width, resources->renderExtent.height, resources->images.size(),
                configuration.sharingMode == VK_SHARING_MODE_EXCLUSIVE ? "EXCLUSIVE" : "CONCURRENT",
                configuration.presentMode == VK_PRESENT_MODE_FIFO_KHR ? " (MAILBOX unavailable in selection)" : "");
    return resources;
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
    passInfo.renderPass = swapchainResources->renderPassHandle;
    passInfo.framebuffer = image.framebufferHandle;
    passInfo.renderArea.extent = swapchainResources->renderExtent;
    passInfo.clearValueCount = 1;
    passInfo.pClearValues = &clearValue;
    vkCmdBeginRenderPass(frame.commandBufferHandle, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(frame.commandBufferHandle);
    checkResult(vkEndCommandBuffer(frame.commandBufferHandle), "record: vkEndCommandBuffer");
}

void VulkanPresenter::renderFrame() {
    const auto state = readWindowState("frame");
    if (state.closing) { return; }
    if (state.iconified || state.framebufferExtent.width == 0 || state.framebufferExtent.height == 0) {
        setPresentationPaused(true, "minimized/zero framebuffer");
        return;
    }
#ifdef ZL_RENDERER_TESTING
    if (shouldRequestSwapchainTestRebuild(submittedFrames, swapchainGeneration)) { rebuildRequested = true; }
#endif
    if (!swapchainResources || rebuildRequested || state.framebufferExtent.width != swapchainResources->framebufferExtent.width ||
        state.framebufferExtent.height != swapchainResources->framebufferExtent.height) {
        if (!rebuildSwapchain()) { return; }
    }
    setPresentationPaused(false, "ready");
    if (swapchainResources->retired) { throw std::runtime_error("acquire: cannot use a retired swapchain."); }
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
    { acquireResult = vkAcquireNextImageKHR(deviceView.deviceHandle, swapchainResources->swapchainHandle, frameWaitNanoseconds,
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
    const auto& image = swapchainResources->images.at(imageIndex);
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
        std::printf("Frame attachments generation=%u framebuffer=%p renderPass=%p\n", swapchainGeneration,
                    reinterpret_cast<void*>(image.framebufferHandle), reinterpret_cast<void*>(swapchainResources->renderPassHandle));
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
    presentInfo.pSwapchains = &swapchainResources->swapchainHandle;
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

bool VulkanPresenter::rebuildSwapchain() {
    rebuildRequested = true;
    std::puts("Rebuilding swapchain");
    checkResult(waitForSubmissions(), "rebuild: vkDeviceWaitIdle");
    for (const auto& frame : frames) {
        checkResult(vkResetCommandPool(deviceView.deviceHandle, frame.commandPoolHandle, 0), "rebuild: vkResetCommandPool");
    }
    if (swapchainResources && swapchainResources->retired) {
        throw std::runtime_error("rebuild: cannot replace a retired swapchain.");
    }
#ifdef ZL_RENDERER_TESTING
    runPresentationPreparationTest(windowHandle, submittedFrames);
#endif
    glfwPollEvents();
    const auto state = readWindowState("rebuild");
    if (state.closing) { return false; }
    if (state.iconified || state.framebufferExtent.width == 0 || state.framebufferExtent.height == 0) {
        setPresentationPaused(true, "minimized/zero framebuffer");
        return false;
    }
    const auto framebufferExtent = state.framebufferExtent;
    auto support = querySurfaceSupport(deviceView.physicalDeviceHandle, deviceView.surfaceHandle);
#ifdef ZL_RENDERER_TESTING
    applySurfaceExtentTest(support.capabilities, submittedFrames);
    if (isVulkanTestCase("fifo") || isVulkanTestCase("fifo-window")) {
        std::erase(support.presentModes, VK_PRESENT_MODE_MAILBOX_KHR);
    }
#endif
    const auto selectedExtent = selectSwapchainExtent(support.capabilities, framebufferExtent);
    if (selectedExtent.width == 0 || selectedExtent.height == 0) {
        setPresentationPaused(true, "zero surface extent");
        glfwWaitEventsTimeout(0.1);
        return false;
    }
    const auto configuration = selectSwapchainConfiguration(support, framebufferExtent, deviceView.queueFamilies);
    auto candidate = createSwapchain(configuration, framebufferExtent,
        swapchainResources ? swapchainResources->swapchainHandle : VK_NULL_HANDLE);
    swapchainResources.swap(candidate); // Unique ownership; candidate now cleans up the retired generation.
    swapchainGeneration = swapchainResources->generation;
    rebuildRequested = false;
    std::printf("Swapchain ready generation=%u framebuffer=%ux%u extent=%ux%u submitted=%llu presented=%llu\n",
                swapchainGeneration, framebufferExtent.width, framebufferExtent.height,
                swapchainResources->renderExtent.width, swapchainResources->renderExtent.height,
                static_cast<unsigned long long>(submittedFrames), static_cast<unsigned long long>(presentedFrames));
#ifdef ZL_RENDERER_TESTING
    std::printf("Swapchain published generation=%u handle=%p\n", swapchainGeneration,
                reinterpret_cast<void*>(swapchainResources->swapchainHandle));
#endif
    return true;
}

void VulkanPresenter::releaseSwapchain(SwapchainResources& resources) noexcept {
    for (auto& image : resources.images) {
        if (image.framebufferHandle) {
            vkDestroyFramebuffer(resources.deviceHandle, image.framebufferHandle, nullptr);
            reportSwapchainResource("Released", "framebuffer", resources.generation, reinterpret_cast<void*>(image.framebufferHandle));
            image.framebufferHandle = VK_NULL_HANDLE;
        }
    }
    for (auto& image : resources.images) {
        if (image.imageViewHandle) {
            vkDestroyImageView(resources.deviceHandle, image.imageViewHandle, nullptr);
            reportSwapchainResource("Released", "image view", resources.generation, reinterpret_cast<void*>(image.imageViewHandle));
            image.imageViewHandle = VK_NULL_HANDLE;
        }
    }
    if (resources.renderPassHandle) {
        vkDestroyRenderPass(resources.deviceHandle, resources.renderPassHandle, nullptr);
        reportSwapchainResource("Released", "render pass", resources.generation, reinterpret_cast<void*>(resources.renderPassHandle));
        resources.renderPassHandle = VK_NULL_HANDLE;
    }
    for (auto& image : resources.images) {
        if (image.renderFinishedSemaphore) {
            vkDestroySemaphore(resources.deviceHandle, image.renderFinishedSemaphore, nullptr);
            reportSwapchainResource("Released", "present semaphore", resources.generation, reinterpret_cast<void*>(image.renderFinishedSemaphore));
            image.renderFinishedSemaphore = VK_NULL_HANDLE;
        }
    }
    resources.images.clear();
    if (resources.swapchainHandle) {
        // Base WSI WaitIdle cleanup limitation is documented in ARCHITECTURE.md.
        vkDestroySwapchainKHR(resources.deviceHandle, resources.swapchainHandle, nullptr);
        reportSwapchainResource("Released", "swapchain", resources.generation, reinterpret_cast<void*>(resources.swapchainHandle));
        resources.swapchainHandle = VK_NULL_HANDLE;
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
    swapchainResources.reset();
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
