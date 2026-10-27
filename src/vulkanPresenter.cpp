#include "vulkanPresenter.h"
#include "swapchainSelection.h"
#include "vulkanUtilities.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cstdio>
#include <exception>
#include <optional>
#include <limits>
#include <sstream>
#include <algorithm>
#ifdef ZL_RENDERER_TESTING
#include "vulkanTestHooks.h"
#include "recordingTestHooks.h"
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
                                 const CompiledRenderGraph& requestedPlan, std::size_t requestedWorkerCount) {
    windowHandle = requestedWindowHandle;
    deviceView = requestedDeviceView;
    graphPlan = &requestedPlan;
    validateGraphRecordingSupport(*graphPlan);
    recordingWorkerCount = std::min(requestedWorkerCount, graphPlan->getGroups().size());
    recordingTasks = std::make_unique<TaskSystem>(recordingWorkerCount);
    std::printf("Recording workers requested=%zu actual=%zu\n", requestedWorkerCount, recordingWorkerCount);
    createFrameResources();
    const auto state = readWindowState("initialize");
    if (!state.closing && !state.iconified && state.framebufferExtent.width > 0 && state.framebufferExtent.height > 0) {
        (void)rebuildSwapchain();
    } else if (!state.closing) {
        setPresentationPaused(true, "minimized/zero framebuffer");
    }
    const auto clearColor = graphPlan->getPasses().back().colorAttachment.clearColor;
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
        const auto laneCount = std::max(std::size_t{1}, recordingWorkerCount);
        const auto groupCount = graphPlan->getGroups().size();
        if (groupCount == 0 || groupCount > std::numeric_limits<std::uint32_t>::max() ||
            laneCount > std::numeric_limits<std::size_t>::max() / groupCount / frames.size()) {
            throw std::runtime_error("frames: command resource count overflow.");
        }
        frame.workerCommands.resize(laneCount);
        for (std::size_t workerIndex = 0; workerIndex < laneCount; ++workerIndex) {
            auto& worker = frame.workerCommands[workerIndex];
            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.queueFamilyIndex = deviceView.queueFamilies.graphicsFamilyIndex;
            VkCommandPool createdPoolHandle = VK_NULL_HANDLE;
            checkResult(vkCreateCommandPool(deviceView.deviceHandle, &poolInfo, nullptr, &createdPoolHandle),
                        "frames: vkCreateCommandPool");
            worker.commandPoolHandle = createdPoolHandle;
            std::puts("Created command pool");
            VkCommandBufferAllocateInfo allocationInfo{};
            allocationInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocationInfo.commandPool = worker.commandPoolHandle;
            allocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocationInfo.commandBufferCount = static_cast<std::uint32_t>(graphPlan->getGroups().size());
            worker.groupCommandBufferHandles.resize(allocationInfo.commandBufferCount, VK_NULL_HANDLE);
#ifdef ZL_RENDERER_TESTING
            runVulkanTestCheckpoint("primary-allocation", VK_NULL_HANDLE);
            if (workerIndex == 1) { runVulkanTestCheckpoint("worker-primary-later", VK_NULL_HANDLE); }
#endif
            checkResult(vkAllocateCommandBuffers(deviceView.deviceHandle, &allocationInfo, worker.groupCommandBufferHandles.data()),
                        "frames: vkAllocateCommandBuffers");
            std::printf("Allocated group primaries count=%u\n", allocationInfo.commandBufferCount);
#ifdef ZL_RENDERER_TESTING
            for (std::size_t groupIndex = 0; groupIndex < worker.groupCommandBufferHandles.size(); ++groupIndex) {
                std::printf("Command resource slot=%zu worker=%zu group=%zu pool=%llu command=%llu\n",
                    static_cast<std::size_t>(&frame - frames.data()), workerIndex, groupIndex,
                    static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(worker.commandPoolHandle)),
                    static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(worker.groupCommandBufferHandles[groupIndex])));
            }
            std::printf("Command lane slot=%zu worker=%zu pool=%llu\n", static_cast<std::size_t>(&frame - frames.data()), workerIndex,
                        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(worker.commandPoolHandle)));
            runVulkanTestCheckpoint(workerIndex == 1 ? "worker-pool-later" : "worker-pool", VK_NULL_HANDLE);
#endif
        }
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
    resources.colorFormat = colorFormat;
    for (const auto& group : graphPlan->getGroups()) {
        const auto& declaration = graphPlan->getPasses().at(group.logicalPassIndices.front()).colorAttachment;
        const auto variant = getAttachmentVariant(declaration);
        if (resources.renderPassHandles[variant]) { continue; }
        const auto attachment = makeGraphAttachment(declaration, colorFormat);
        const VkAttachmentReference colorReference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;
        VkRenderPassCreateInfo passInfo{};
        passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        passInfo.attachmentCount = 1;
        passInfo.pAttachments = &attachment;
        passInfo.subpassCount = 1;
        passInfo.pSubpasses = &subpass;
        VkRenderPass createdPassHandle = VK_NULL_HANDLE;
        checkResult(vkCreateRenderPass(deviceView.deviceHandle, &passInfo, nullptr, &createdPassHandle), "swapchain: vkCreateRenderPass");
        resources.renderPassHandles[variant] = createdPassHandle;
        reportSwapchainResource("Created", "render pass", resources.generation, reinterpret_cast<void*>(createdPassHandle));
#ifdef ZL_RENDERER_TESTING
        std::printf("Graph attachment generation=%u variant=%zu load=%d store=%d initialLayout=%d finalLayout=%d dependencies=%u\n",
            resources.generation, variant, attachment.loadOp, attachment.storeOp, attachment.initialLayout,
            attachment.finalLayout, passInfo.dependencyCount);
        runSwapchainTestCheckpoint("render-pass", resources.generation);
#endif
    }
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
        framebufferInfo.renderPass = resources->renderPassHandles[getAttachmentVariant(graphPlan->getPasses().front().colorAttachment)];
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

VulkanPresenter::RecordedGraphFrame VulkanPresenter::recordGraphFrame(const FrameResources& frame,
    const PreparedGraphFrame& prepared, bool captureTrace) {
    std::vector<RecordedRenderGroup> recordedGroups(prepared.groups.size());
    std::vector<TaskSystem::Task> tasks;
    tasks.reserve(prepared.groups.size());
#ifdef ZL_RENDERER_TESTING
    beginRecordingTestBatch(submittedFrames, prepared.groups.size(), recordingWorkerCount);
#endif
    for (std::size_t groupIndex = 0; groupIndex < prepared.groups.size(); ++groupIndex) {
        tasks.emplace_back([&, groupIndex](std::size_t workerIndex) {
            const auto& worker = frame.workerCommands.at(workerIndex);
            auto& result = recordedGroups[groupIndex];
            result.frameSlot = prepared.frameSlot;
            result.generation = prepared.generation;
            result.acquiredImageIndex = prepared.acquiredImageIndex;
            result.planVersion = prepared.planVersion;
            result.groupIndex = groupIndex;
            result.workerIndex = workerIndex;
            result.commandPoolHandle = worker.commandPoolHandle;
            result.commandBufferHandle = worker.groupCommandBufferHandles.at(groupIndex);
            if (captureTrace) {
                std::ostringstream threadName;
                threadName << std::this_thread::get_id();
                result.threadIdentity = threadName.str();
            }
            result.trace = recordRenderGroup(prepared.groups[groupIndex], result.commandBufferHandle, captureTrace);
            result.complete = true;
        });
    }
    for (const auto& worker : frame.workerCommands) {
#ifdef ZL_RENDERER_TESTING
        if (vkGetFenceStatus(deviceView.deviceHandle, frame.submissionFence) != VK_SUCCESS) {
            throw std::runtime_error("record reset before slot GPU completion");
        }
#endif
        checkResult(vkResetCommandPool(deviceView.deviceHandle, worker.commandPoolHandle, 0), "record: vkResetCommandPool");
    }
#ifdef ZL_RENDERER_TESTING
    runVulkanTestCheckpoint("record", VK_NULL_HANDLE);
#endif
    // Nothing between accepting this batch and waiting can throw or invalidate its borrowed storage.
    const auto batch = recordingTasks->submitBatch(std::move(tasks));
    const auto& batchResult = batch.wait();
#ifdef ZL_RENDERER_TESTING
    finishRecordingTestBatch();
    if (captureTrace) {
        for (const auto& result : recordedGroups) {
            if (result.complete) { std::printf("Recorded physical group=%zu\n", result.groupIndex); }
        }
    }
#endif
    if (batchResult.failedTaskCount) {
        std::string failures;
        for (std::size_t groupIndex = 0; groupIndex < batchResult.taskExceptions.size(); ++groupIndex) {
            if (!batchResult.taskExceptions[groupIndex]) { continue; }
            failures += " record group=" + std::to_string(groupIndex) + ": ";
            try { std::rethrow_exception(batchResult.taskExceptions[groupIndex]); }
            catch (const std::exception& error) { failures += error.what(); }
            catch (...) { failures += "non-standard exception"; }
        }
        throw std::runtime_error(failures);
    }
    RecordedGraphFrame recordedFrame;
    recordedFrame.orderedCommands = collectRecordedGroupCommands(prepared, recordedGroups);
    for (const auto& result : recordedGroups) {
        if (captureTrace) {
            std::ostringstream identity;
            identity << "Graph worker group=" << result.groupIndex << " worker=" << result.workerIndex
                << " thread=" << result.threadIdentity << " pool=" << reinterpret_cast<std::uintptr_t>(result.commandPoolHandle)
                << " command=" << reinterpret_cast<std::uintptr_t>(result.commandBufferHandle) << '\n';
            recordedFrame.trace += identity.str();
        }
        recordedFrame.trace += result.trace;
    }
    return recordedFrame;
}

std::string VulkanPresenter::recordRenderGroup(const PreparedRenderGroup& group, VkCommandBuffer commandBufferHandle, bool captureTrace) {
    const auto groupIndex = group.groupIndex;
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    std::optional<std::ostringstream> trace;
    if (captureTrace) { trace.emplace(); }
    const auto emitTransitions = [&](VkCommandBuffer commandBufferHandle, std::size_t groupIndex, const char* boundary,
                                     const std::vector<PreparedGraphBarrier>& transitions) {
        for (const auto& transition : transitions) {
            const auto& mapped = transition.mapped;
            vkCmdPipelineBarrier(commandBufferHandle, mapped.sourceStages, mapped.destinationStages, 0,
                                 0, nullptr, 0, nullptr, 1, &mapped.imageBarrier);
            if (trace) {
                const auto& barrier = mapped.imageBarrier;
                *trace << "Graph barrier group=" << groupIndex << " boundary=" << boundary << " resource=" << transition.imageIndex
                       << " imageHandle=" << reinterpret_cast<std::uintptr_t>(barrier.image)
                       << " oldLayout=" << barrier.oldLayout << " newLayout=" << barrier.newLayout
                       << " srcStage=" << mapped.sourceStages << " dstStage=" << mapped.destinationStages
                       << " srcAccess=" << barrier.srcAccessMask << " dstAccess=" << barrier.dstAccessMask
                       << " reasons=" << describeDependencyReasons(transition.reasons) << '\n';
            }
        }
    };
#ifdef ZL_RENDERER_TESTING
    runRecordingGroupCheckpoint("group-begin", groupIndex);
#endif
    checkResult(vkBeginCommandBuffer(commandBufferHandle, &beginInfo), "record: vkBeginCommandBuffer");
#ifdef ZL_RENDERER_TESTING
    RecordingTestScope recordingScope(groupIndex);
#endif
    if (trace) { *trace << "Graph primary begin group=" << groupIndex << " command=" << reinterpret_cast<std::uintptr_t>(commandBufferHandle) << '\n'; }
    const auto& attachment = group.attachment;
    const auto& binding = group.binding;
    emitTransitions(commandBufferHandle, groupIndex, "before", group.beforeBarriers);
    const auto& clearColor = attachment.clearColor;
    const VkClearValue clearValue{{{clearColor.red, clearColor.green, clearColor.blue, clearColor.alpha}}};
    VkRenderPassBeginInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passInfo.renderPass = group.renderPassHandle;
    passInfo.framebuffer = binding.framebufferHandle;
    passInfo.renderArea.extent = binding.extent;
    passInfo.clearValueCount = 1;
    passInfo.pClearValues = &clearValue;
    vkCmdBeginRenderPass(commandBufferHandle, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
    {
        if (trace) {
            const auto& actualAttachment = group.attachmentDescription;
            *trace << "Graph group group=" << groupIndex << " pass=" << group.passName
                   << " resource=" << attachment.colorTargetHandle.getIndex()
                   << " imageHandle=" << reinterpret_cast<std::uintptr_t>(binding.imageHandle)
                   << " framebuffer=" << reinterpret_cast<std::uintptr_t>(passInfo.framebuffer)
                   << " renderPass=" << reinterpret_cast<std::uintptr_t>(passInfo.renderPass)
                   << " extent=" << passInfo.renderArea.extent.width << 'x' << passInfo.renderArea.extent.height
                   << " load=" << actualAttachment.loadOp << " store=" << actualAttachment.storeOp
                   << " initialLayout=" << actualAttachment.initialLayout << " finalLayout=" << actualAttachment.finalLayout
                   << " clearColor=(" << clearValue.color.float32[0] << ',' << clearValue.color.float32[1] << ','
                   << clearValue.color.float32[2] << ',' << clearValue.color.float32[3] << ")\n";
        }
        // This change's pass body is empty: attachment load/store performs the clear/preserve work.
    }
#ifdef ZL_RENDERER_TESTING
    runRecordingGroupCheckpoint("group-body", groupIndex);
#endif
    vkCmdEndRenderPass(commandBufferHandle);
#ifdef ZL_RENDERER_TESTING
    runRecordingGroupCheckpoint("graph-group", groupIndex);
#endif
    emitTransitions(commandBufferHandle, groupIndex, "after", group.afterBarriers);
#ifdef ZL_RENDERER_TESTING
    runRecordingGroupCheckpoint("group-end", groupIndex);
    beforeRecordingTestEnd(groupIndex);
#endif
    checkResult(vkEndCommandBuffer(commandBufferHandle), "record: vkEndCommandBuffer");
    if (trace) { *trace << "Graph primary end group=" << groupIndex << " command=" << reinterpret_cast<std::uintptr_t>(commandBufferHandle) << '\n'; }
#ifdef ZL_RENDERER_TESTING
    recordingScope.complete();
#endif
    return trace ? trace->str() : std::string{};
}

std::string VulkanPresenter::submitGraphFrame(const FrameResources& frame, const PreparedGraphFrame& prepared,
    std::span<const VkCommandBuffer> orderedCommands, VkSemaphore renderFinishedSemaphore, bool captureTrace) {
    const auto waitStage = prepared.acquireWaitStage;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &frame.imageAvailableSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = static_cast<std::uint32_t>(orderedCommands.size());
    submitInfo.pCommandBuffers = orderedCommands.data();
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinishedSemaphore;
    std::ostringstream trace;
    if (captureTrace) {
        trace << "Graph submit inputs count=" << submitInfo.commandBufferCount << " waits=" << submitInfo.waitSemaphoreCount
              << " signals=" << submitInfo.signalSemaphoreCount << '\n';
        for (std::uint32_t commandIndex = 0; commandIndex < submitInfo.commandBufferCount; ++commandIndex) {
            trace << "Graph submit command index=" << commandIndex << " command="
                  << reinterpret_cast<std::uintptr_t>(submitInfo.pCommandBuffers[commandIndex]) << '\n';
        }
    }
    auto submitTrace = trace.str();
#ifdef ZL_RENDERER_TESTING
    runVulkanTestCheckpoint("reset-fence", VK_NULL_HANDLE);
    if (captureTrace) { std::puts("Resetting submission fence"); }
#endif
    checkResult(vkResetFences(deviceView.deviceHandle, 1, &frame.submissionFence), "submit: vkResetFences");
    VkResult submitResult;
#ifdef ZL_RENDERER_TESTING
    if (isVulkanTestCase("submit") || (isVulkanTestCase("submit-later") && submittedFrames == 3)) { submitResult = VK_ERROR_INITIALIZATION_FAILED; }
    else
#endif
    { submitResult = vkQueueSubmit(deviceView.graphicsQueueHandle, 1, &submitInfo, frame.submissionFence); }
    checkResult(submitResult, "submit: vkQueueSubmit");
    return submitTrace;
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
                               isVulkanTestCase("surface-lost") || isVulkanTestCase("acquire-not-ready"))) {
        testInjectionUsed = true;
        acquireResult = isVulkanTestCase("acquire-not-ready") ? VK_NOT_READY : isVulkanTestCase("surface-lost") ? VK_ERROR_SURFACE_LOST_KHR :
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
    std::array bindings{GraphImageBinding{graphPlan->getPresentTargetHandle(), image.imageHandle, image.imageViewHandle,
        image.framebufferHandle, swapchainResources->colorFormat, swapchainResources->renderExtent, swapchainGeneration, imageIndex}};
#ifdef ZL_RENDERER_TESTING
    runVulkanTestCheckpoint("graph-binding", VK_NULL_HANDLE);
    if (isVulkanTestCase("graph-stale")) { --bindings[0].generation; }
    if (isVulkanTestCase("graph-missing")) { bindings[0].framebufferHandle = VK_NULL_HANDLE; }
#endif
    validateGraphBindings(*graphPlan, bindings, {swapchainGeneration, imageIndex, swapchainResources->colorFormat,
                                               swapchainResources->renderExtent});
    const auto reportPlan = reportedGeneration != swapchainGeneration || reportedPlanVersion != graphPlan->getVersion();
    auto captureTrace = reportPlan;
#ifdef ZL_RENDERER_TESTING
    captureTrace = captureTrace || submittedFrames < 12;
#endif
    const auto prepared = prepareGraphFrame(*graphPlan, bindings,
        {swapchainGeneration, imageIndex, swapchainResources->colorFormat, swapchainResources->renderExtent},
        frameIndex, swapchainResources->renderPassHandles);
    const auto recordedFrame = recordGraphFrame(frame, prepared, captureTrace);
    const auto submitTrace = submitGraphFrame(frame, prepared, recordedFrame.orderedCommands, image.renderFinishedSemaphore, captureTrace);
    hasSubmittedWork = true;
    ++submittedFrames;
    if (reportPlan) {
        std::printf("Graph plan generation=%u version=%llu slot=%u image=%u groups=%zu\n", swapchainGeneration,
            static_cast<unsigned long long>(graphPlan->getVersion()), frameIndex, imageIndex, graphPlan->getGroups().size());
        for (std::size_t resourceIndex = 0; resourceIndex < graphPlan->getImages().size(); ++resourceIndex) {
            const auto& declaration = graphPlan->getImages()[resourceIndex];
            std::printf("Graph resource index=%zu name=%s ownership=external initialUse=%s contentsDefined=%d\n",
                resourceIndex, declaration.name.c_str(), getImageUseName(declaration.initialUse), declaration.contentsDefined ? 1 : 0);
        }
        for (const auto& dependency : graphPlan->getDependencies()) {
            const auto sourceName = dependency.sourcePassIndex == externalPassIndex ? "external" :
                graphPlan->getPasses()[dependency.sourcePassIndex].name.c_str();
            const auto destinationName = dependency.destinationPassIndex == graphPlan->getPasses().size() ? "present" :
                graphPlan->getPasses()[dependency.destinationPassIndex].name.c_str();
            std::printf("Graph dependency resource=%zu source=%s destination=%s reasons=%s\n", dependency.imageIndex,
                sourceName, destinationName, describeDependencyReasons(dependency.reasons).c_str());
        }
        reportedGeneration = swapchainGeneration;
        reportedPlanVersion = graphPlan->getVersion();
    }
    if (captureTrace) {
        std::printf("Graph submitted frame=%llu generation=%u version=%llu slot=%u image=%u groups=%zu submits=1 waitStage=%u\n%s",
            static_cast<unsigned long long>(submittedFrames), swapchainGeneration,
            static_cast<unsigned long long>(graphPlan->getVersion()), frameIndex, imageIndex, graphPlan->getGroups().size(), prepared.acquireWaitStage, recordedFrame.trace.c_str());
        std::printf("%s", submitTrace.c_str());
        std::puts("Graph record end");
    }
#ifdef ZL_RENDERER_TESTING
    if (submittedFrames <= 12) {
        std::printf("Frame attachments generation=%u framebuffer=%p renderPass=%p\n", swapchainGeneration,
                    reinterpret_cast<void*>(image.framebufferHandle), reinterpret_cast<void*>(swapchainResources->renderPassHandles[
                        getAttachmentVariant(graphPlan->getPasses().front().colorAttachment)]));
        std::printf("Frame submitted slot=%u image=%u command=%p acquire=%p present=%p fence=%p generation=%u\n", frameIndex, imageIndex,
                    static_cast<void*>(recordedFrame.orderedCommands.front()), reinterpret_cast<void*>(frame.imageAvailableSemaphore),
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
    if (captureTrace) {
        std::printf("Graph present request frame=%llu generation=%u image=%u result=%d\n",
            static_cast<unsigned long long>(submittedFrames), swapchainGeneration, imageIndex, presentResult);
    }
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
    if (submittedFrames >= 12 && !isVulkanTestCase("alternate-window") && !isVulkanTestCase("fifo-window") &&
        !isVulkanTestCase("graph-double-window")) {
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
        for (const auto& worker : frame.workerCommands) {
            checkResult(vkResetCommandPool(deviceView.deviceHandle, worker.commandPoolHandle, 0), "rebuild: vkResetCommandPool");
#ifdef ZL_RENDERER_TESTING
            std::printf("Rebuild pool reset slot=%zu worker=%zu\n", static_cast<std::size_t>(&frame - frames.data()),
                        static_cast<std::size_t>(&worker - frame.workerCommands.data()));
#endif
        }
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
    for (auto& renderPassHandle : resources.renderPassHandles) {
        if (!renderPassHandle) { continue; }
        vkDestroyRenderPass(resources.deviceHandle, renderPassHandle, nullptr);
        reportSwapchainResource("Released", "render pass", resources.generation, reinterpret_cast<void*>(renderPassHandle));
        renderPassHandle = VK_NULL_HANDLE;
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
        for (auto& worker : frame.workerCommands) {
            if (worker.commandPoolHandle) {
                vkDestroyCommandPool(deviceView.deviceHandle, worker.commandPoolHandle, nullptr);
                worker.commandPoolHandle = VK_NULL_HANDLE;
                worker.groupCommandBufferHandles.clear();
                std::puts("Released command pool");
            }
        }
        frame.workerCommands.clear();
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
    if (recordingTasks) {
        recordingTasks->shutdown();
        recordingTasks.reset();
        std::puts("Recording workers drained and joined");
    }
    if (!deviceView.deviceHandle) { return; }
#ifdef ZL_RENDERER_TESTING
    verifyRecordingWorkersStopped();
#endif
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
