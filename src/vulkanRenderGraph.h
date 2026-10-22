#pragma once
#include "renderGraph.h"
#include <span>
#include <vulkan/vulkan.h>

struct VulkanImageUse {
    VkImageLayout layout;
    VkPipelineStageFlags stages;
    VkAccessFlags access;
};
struct VulkanGraphBarrier {
    VkPipelineStageFlags sourceStages;
    VkPipelineStageFlags destinationStages;
    VkImageMemoryBarrier imageBarrier;
};
struct GraphImageBinding {
    GraphImageHandle graphImageHandle;
    VkImage imageHandle = VK_NULL_HANDLE;
    VkImageView imageViewHandle = VK_NULL_HANDLE;
    VkFramebuffer framebufferHandle = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::uint32_t generation = 0;
    std::uint32_t imageIndex = 0;
};
struct SwapchainBindingContext {
    std::uint32_t generation;
    std::uint32_t acquiredImageIndex;
    VkFormat format;
    VkExtent2D extent;
};

VulkanImageUse mapImageUse(ImageUse use);
VulkanGraphBarrier mapGraphTransition(const GraphImageTransition& transition, VkImage imageHandle);
VkAttachmentDescription makeGraphAttachment(const ColorAttachmentDeclaration& attachment, VkFormat format);
std::size_t getAttachmentVariant(const ColorAttachmentDeclaration& attachment);
VkPipelineStageFlags getGraphAcquireWaitStage(const CompiledRenderGraph& plan);
void validateGraphRecordingSupport(const CompiledRenderGraph& plan);
void validateGraphBindings(const CompiledRenderGraph& plan, std::span<const GraphImageBinding> bindings,
                           const SwapchainBindingContext& context);

// Per-frame snapshots borrow handles; no pointers into movable native structures.
struct PreparedGraphBarrier {
    VulkanGraphBarrier mapped;
    std::size_t imageIndex;
    std::uint32_t reasons;
};
struct PreparedRenderGroup {
    std::size_t groupIndex;
    std::string passName;
    ColorAttachmentDeclaration attachment;
    GraphImageBinding binding;
    VkRenderPass renderPassHandle;
    VkAttachmentDescription attachmentDescription;
    std::vector<PreparedGraphBarrier> beforeBarriers;
    std::vector<PreparedGraphBarrier> afterBarriers;
};
struct PreparedGraphFrame {
    std::uint32_t frameSlot;
    std::uint32_t generation;
    std::uint32_t acquiredImageIndex;
    std::uint64_t planVersion;
    VkPipelineStageFlags acquireWaitStage;
    std::vector<PreparedRenderGroup> groups;
};
PreparedGraphFrame prepareGraphFrame(const CompiledRenderGraph& plan, std::span<const GraphImageBinding> bindings,
    const SwapchainBindingContext& context, std::uint32_t frameSlot, std::span<const VkRenderPass> renderPassHandles);
