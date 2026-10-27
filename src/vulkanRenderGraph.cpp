#include "vulkanRenderGraph.h"
#include <algorithm>
#include <stdexcept>

std::vector<VkCommandBuffer> collectRecordedGroupCommands(const PreparedGraphFrame& prepared,
    std::span<const RecordedRenderGroup> recordedGroups) {
    if (recordedGroups.size() != prepared.groups.size() || recordedGroups.empty()) {
        throw std::runtime_error("record results: missing groups.");
    }
    std::vector<VkCommandBuffer> orderedCommands(recordedGroups.size(), VK_NULL_HANDLE);
    for (const auto& recorded : recordedGroups) {
        if (!recorded.complete || !recorded.commandBufferHandle || !recorded.commandPoolHandle ||
            recorded.groupIndex >= orderedCommands.size() || orderedCommands[recorded.groupIndex] ||
            recorded.frameSlot != prepared.frameSlot || recorded.generation != prepared.generation ||
            recorded.acquiredImageIndex != prepared.acquiredImageIndex || recorded.planVersion != prepared.planVersion ||
            std::find(orderedCommands.begin(), orderedCommands.end(), recorded.commandBufferHandle) != orderedCommands.end()) {
            throw std::runtime_error("record results: invalid or duplicate group/primary/frame identity.");
        }
        orderedCommands[recorded.groupIndex] = recorded.commandBufferHandle;
    }
    return orderedCommands;
}

VulkanImageUse mapImageUse(ImageUse use) {
    switch (use) {
    case ImageUse::undefined: return {VK_IMAGE_LAYOUT_UNDEFINED, 0, 0};
    case ImageUse::colorAttachmentWrite:
        return {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
    case ImageUse::colorAttachmentReadWrite:
        return {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
    case ImageUse::fragmentSampledRead:
        return {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT};
    case ImageUse::present: return {VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0};
    }
    throw std::runtime_error("graph mapping: unsupported image use.");
}

VulkanGraphBarrier mapGraphTransition(const GraphImageTransition& transition, VkImage imageHandle) {
    if (!imageHandle || transition.nextUse == ImageUse::undefined) {
        throw std::runtime_error("graph mapping: missing image or invalid target state.");
    }
    const auto target = mapImageUse(transition.nextUse);
    VulkanGraphBarrier result{};
    result.destinationStages = target.stages;
    auto& barrier = result.imageBarrier;
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = mapImageUse(transition.previousUse).layout;
    barrier.newLayout = target.layout;
    barrier.image = imageHandle;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    for (const auto sourceUse : {ImageUse::colorAttachmentWrite, ImageUse::colorAttachmentReadWrite, ImageUse::fragmentSampledRead}) {
        if ((transition.sourceUses & useBit(sourceUse)) == 0) { continue; }
        const auto source = mapImageUse(sourceUse);
        result.sourceStages |= source.stages;
        barrier.srcAccessMask |= source.access;
    }
    // External availability is supplied by the acquire semaphore, at the first actual use.
    if (result.sourceStages == 0) { result.sourceStages = target.stages; }
    barrier.dstAccessMask = target.access;
    const auto memoryReasons = reasonBit(DependencyReason::readAfterWrite) | reasonBit(DependencyReason::writeAfterWrite) |
                               reasonBit(DependencyReason::layoutTransition) | reasonBit(DependencyReason::presentation) |
                               reasonBit(DependencyReason::external);
    if ((transition.reasons & memoryReasons) == 0) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = 0;
    }
    return result;
}

std::size_t getAttachmentVariant(const ColorAttachmentDeclaration& attachment) {
    return (attachment.load == AttachmentLoad::load ? 2u : 0u) + (attachment.store == AttachmentStore::discard ? 1u : 0u);
}

VkAttachmentDescription makeGraphAttachment(const ColorAttachmentDeclaration& attachment, VkFormat format) {
    VkAttachmentDescription description{};
    description.format = format;
    description.samples = VK_SAMPLE_COUNT_1_BIT;
    description.loadOp = attachment.load == AttachmentLoad::clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    description.storeOp = attachment.store == AttachmentStore::store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    description.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    description.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    description.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    return description;
}

VkPipelineStageFlags getGraphAcquireWaitStage(const CompiledRenderGraph& plan) {
    const auto targetIndex = plan.getPresentImageIndex();
    for (const auto& pass : plan.getPasses()) {
        if (std::any_of(pass.sampledImages.begin(), pass.sampledImages.end(), [&](auto handle) { return handle.getIndex() == targetIndex; })) {
            return mapImageUse(ImageUse::fragmentSampledRead).stages;
        }
        if (pass.colorAttachment.colorTargetHandle.getIndex() == targetIndex) {
            return mapImageUse(ImageUse::colorAttachmentWrite).stages;
        }
    }
    throw std::runtime_error("graph mapping: presentation target has no rendering use.");
}

void validateGraphRecordingSupport(const CompiledRenderGraph& plan) {
    if (plan.getGroups().empty()) { throw std::runtime_error("graph record: an executable plan is required."); }
    for (const auto& pass : plan.getPasses()) {
        if (!pass.sampledImages.empty()) {
            throw std::runtime_error("graph record " + pass.name + ": sampled drawing is not implemented.");
        }
    }
}

void validateGraphBindings(const CompiledRenderGraph& plan, std::span<const GraphImageBinding> bindings,
                           const SwapchainBindingContext& context) {
    if (context.generation == 0 || bindings.size() != plan.getImages().size()) {
        throw std::runtime_error("graph binding: missing acquired image or resource bindings.");
    }
    for (std::size_t index = 0; index < bindings.size(); ++index) {
        const auto& binding = bindings[index];
        const auto& description = plan.getImages()[index];
        if (binding.graphImageHandle.getGraphIdentity() != plan.getIdentity() || binding.graphImageHandle.getIndex() != index ||
            !binding.imageHandle || !binding.imageViewHandle || binding.format == VK_FORMAT_UNDEFINED ||
            binding.extent.width == 0 || binding.extent.height == 0) {
            throw std::runtime_error("graph binding " + description.name + ": invalid image reference or description.");
        }
        if (description.role == ExternalImageRole::swapchainColor &&
            (binding.generation != context.generation || binding.imageIndex != context.acquiredImageIndex ||
             binding.format != context.format || binding.extent.width != context.extent.width || binding.extent.height != context.extent.height)) {
            throw std::runtime_error("graph binding " + description.name + ": stale or incompatible swapchain image.");
        }
        for (std::size_t previousIndex = 0; previousIndex < index; ++previousIndex) {
            if (bindings[previousIndex].imageHandle == binding.imageHandle) {
                throw std::runtime_error("graph binding " + description.name + ": physical aliasing is unsupported.");
            }
        }
        for (const auto& pass : plan.getPasses()) {
            if (pass.colorAttachment.colorTargetHandle.getIndex() == index && !binding.framebufferHandle) {
                throw std::runtime_error("graph binding " + description.name + ": missing framebuffer.");
            }
        }
    }
}

PreparedGraphFrame prepareGraphFrame(const CompiledRenderGraph& plan, std::span<const GraphImageBinding> bindings,
    const SwapchainBindingContext& context, std::uint32_t frameSlot, std::span<const VkRenderPass> renderPassHandles) {
    validateGraphRecordingSupport(plan);
    validateGraphBindings(plan, bindings, context);
    PreparedGraphFrame prepared{frameSlot, context.generation, context.acquiredImageIndex, plan.getVersion(),
                                getGraphAcquireWaitStage(plan), {}};
    prepared.groups.reserve(plan.getGroups().size());
    for (std::size_t groupIndex = 0; groupIndex < plan.getGroups().size(); ++groupIndex) {
        const auto& group = plan.getGroups()[groupIndex];
        if (group.logicalPassIndices.size() != 1) {
            throw std::runtime_error("graph preparation: only one raster pass per physical group is supported.");
        }
        const auto& pass = plan.getPasses().at(group.logicalPassIndices.front());
        const auto& binding = bindings[pass.colorAttachment.colorTargetHandle.getIndex()];
        const auto variant = getAttachmentVariant(pass.colorAttachment);
        if (variant >= renderPassHandles.size() || !renderPassHandles[variant]) {
            throw std::runtime_error("graph preparation: missing render pass variant.");
        }
        PreparedRenderGroup preparedGroup{groupIndex, pass.name, pass.colorAttachment, binding,
            renderPassHandles[variant], makeGraphAttachment(pass.colorAttachment, binding.format), {}, {}};
        const auto prepareBarriers = [&](const auto& transitions, auto& barriers) {
            for (const auto& transition : transitions) {
                barriers.push_back({mapGraphTransition(transition, bindings[transition.imageIndex].imageHandle),
                                    transition.imageIndex, transition.reasons});
            }
        };
        prepareBarriers(group.beforeTransitions, preparedGroup.beforeBarriers);
        prepareBarriers(group.afterTransitions, preparedGroup.afterBarriers);
        prepared.groups.push_back(std::move(preparedGroup));
    }
    return prepared;
}
