#include "vulkanRenderGraph.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* description) {
    if (!condition) { throw std::runtime_error(description); }
}
template<class Operation>
void expectFailure(Operation operation, const char* messagePart) {
    try { operation(); }
    catch (const std::runtime_error& error) {
        require(std::string(error.what()).find(messagePart) != std::string::npos, error.what());
        return;
    }
    throw std::runtime_error("Expected backend rejection did not occur.");
}
void mapping() {
    const auto imageHandle = reinterpret_cast<VkImage>(std::uintptr_t{11}); // Host-only fixture; never submitted.
    const auto colorUse = ImageUse::colorAttachmentWrite;
    const auto externalReasons = reasonBit(DependencyReason::external) | reasonBit(DependencyReason::layoutTransition);
    const auto acquire = mapGraphTransition({0, ImageUse::undefined, colorUse, useBit(ImageUse::undefined), externalReasons}, imageHandle);
    require(acquire.sourceStages == VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT &&
            acquire.destinationStages == acquire.sourceStages && acquire.imageBarrier.srcAccessMask == 0 &&
            acquire.imageBarrier.dstAccessMask == VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, "Acquire stage chain is broken");
    const auto waw = mapGraphTransition({0, colorUse, colorUse, useBit(colorUse), reasonBit(DependencyReason::writeAfterWrite)}, imageHandle);
    require(waw.imageBarrier.oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
            waw.imageBarrier.newLayout == waw.imageBarrier.oldLayout && waw.imageBarrier.srcAccessMask == VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT &&
            waw.imageBarrier.dstAccessMask == VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, "Same-layout WAW must keep memory dependency");
    const auto raw = mapGraphTransition({0, colorUse, ImageUse::fragmentSampledRead, useBit(colorUse),
        reasonBit(DependencyReason::readAfterWrite) | reasonBit(DependencyReason::layoutTransition)}, imageHandle);
    require(raw.sourceStages == VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT && raw.destinationStages == VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT &&
            raw.imageBarrier.dstAccessMask == VK_ACCESS_SHADER_READ_BIT && raw.imageBarrier.newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            "Wrong color-to-fragment mapping");
    const auto war = mapGraphTransition({0, ImageUse::fragmentSampledRead, colorUse, useBit(ImageUse::fragmentSampledRead),
        reasonBit(DependencyReason::writeAfterRead) | reasonBit(DependencyReason::layoutTransition)}, imageHandle);
    require(war.sourceStages == VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT && war.destinationStages == VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT &&
            war.imageBarrier.oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && war.imageBarrier.dstAccessMask == VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            "WAR with layout change still requires a valid transition");
    const auto executionOnly = mapGraphTransition({0, colorUse, colorUse, useBit(colorUse), reasonBit(DependencyReason::writeAfterRead)}, imageHandle);
    require(executionOnly.imageBarrier.srcAccessMask == 0 && executionOnly.imageBarrier.dstAccessMask == 0,
            "Pure WAR needs execution ordering rather than invented visibility");
    const auto present = mapGraphTransition({0, colorUse, ImageUse::present, useBit(colorUse), reasonBit(DependencyReason::presentation)}, imageHandle);
    require(present.destinationStages == VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT && present.imageBarrier.dstAccessMask == 0 &&
            present.imageBarrier.newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR && present.imageBarrier.image == imageHandle,
            "Wrong legacy present transition");
    require(present.imageBarrier.srcQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED &&
            present.imageBarrier.dstQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED && present.imageBarrier.subresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT &&
            present.imageBarrier.subresourceRange.levelCount == 1 && present.imageBarrier.subresourceRange.layerCount == 1,
            "Do not invent a queue transfer or widen image range");
    ColorAttachmentDeclaration attachment;
    for (const auto load : {AttachmentLoad::clear, AttachmentLoad::load}) {
        for (const auto store : {AttachmentStore::store, AttachmentStore::discard}) {
            attachment.load = load;
            attachment.store = store;
            const auto description = makeGraphAttachment(attachment, VK_FORMAT_B8G8R8A8_SRGB);
            require(description.initialLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && description.finalLayout == description.initialLayout,
                    "Render pass must not duplicate graph transitions");
            require(description.loadOp == (load == AttachmentLoad::clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD) &&
                    description.storeOp == (store == AttachmentStore::store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE),
                    "Preserve group load/store intent");
        }
    }
    const auto readWrite = mapImageUse(ImageUse::colorAttachmentReadWrite);
    require(readWrite.access == (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT), "LOAD also reads");
    expectFailure([&] { (void)mapGraphTransition({0, colorUse, ImageUse::present, useBit(colorUse), 0}, VK_NULL_HANDLE); }, "missing image");
}

GraphImageBinding makeBinding(GraphImageHandle imageHandle, std::uintptr_t identity) {
    return {imageHandle, reinterpret_cast<VkImage>(identity), reinterpret_cast<VkImageView>(identity + 100),
            reinterpret_cast<VkFramebuffer>(identity + 200), VK_FORMAT_B8G8R8A8_SRGB, {640,480}, 3, 2};
}
void bindings() {
    RenderGraph graph;
    const auto targetHandle = graph.importImage({"swapchain", ExternalImageRole::swapchainColor});
    graph.addRasterPass({"clear", {targetHandle}, {}});
    graph.setPresentTarget(targetHandle);
    const auto plan = graph.compile();
    const SwapchainBindingContext context{3, 2, VK_FORMAT_B8G8R8A8_SRGB, {640,480}};
    const std::array valid{makeBinding(targetHandle, 11)};
    validateGraphBindings(plan, valid, context);
    require(getGraphAcquireWaitStage(plan) == VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, "Wait for actual first use");
    expectFailure([&] { validateGraphBindings(plan, {}, context); }, "missing");
    auto invalid = valid;
    invalid[0].generation = 2;
    expectFailure([&] { validateGraphBindings(plan, invalid, context); }, "stale");
    invalid = valid; invalid[0].imageIndex = 1;
    expectFailure([&] { validateGraphBindings(plan, invalid, context); }, "stale");
    invalid = valid; invalid[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    expectFailure([&] { validateGraphBindings(plan, invalid, context); }, "incompatible");
    invalid = valid; invalid[0].extent.width = 320;
    expectFailure([&] { validateGraphBindings(plan, invalid, context); }, "incompatible");
    invalid = valid; invalid[0].framebufferHandle = VK_NULL_HANDLE;
    expectFailure([&] { validateGraphBindings(plan, invalid, context); }, "framebuffer");
    RenderGraph otherGraph;
    invalid = valid; invalid[0].graphImageHandle = otherGraph.importImage({"other"});
    expectFailure([&] { validateGraphBindings(plan, invalid, context); }, "reference");
    const auto secondHandle = graph.importImage({"second"});
    graph.addRasterPass({"second", {secondHandle}, {}});
    const auto twoImagePlan = graph.compile();
    const std::array aliased{valid[0], makeBinding(secondHandle, 11)};
    expectFailure([&] { validateGraphBindings(twoImagePlan, aliased, context); }, "aliasing");
    const std::array distinct{valid[0], makeBinding(secondHandle, 12)};
    validateGraphBindings(twoImagePlan, distinct, context);
}
void recording() {
    RenderGraph graph;
    const auto targetHandle = graph.importImage({"swapchain", ExternalImageRole::swapchainColor});
    graph.addRasterPass({"clearA", {targetHandle}, {}});
    graph.addRasterPass({"clearB", {targetHandle}, {}});
    graph.setPresentTarget(targetHandle);
    const auto plan = graph.compile();
    validateGraphRecordingSupport(plan);
    const std::array bindings{makeBinding(targetHandle, 11)};
    const SwapchainBindingContext context{3, 2, VK_FORMAT_B8G8R8A8_SRGB, {640,480}};
    const std::array renderPasses{reinterpret_cast<VkRenderPass>(std::uintptr_t{300})};
    const auto prepared = prepareGraphFrame(plan, bindings, context, 0, renderPasses);
    require(prepared.frameSlot == 0 && prepared.acquiredImageIndex == 2 && prepared.generation == 3 &&
            prepared.planVersion == plan.getVersion() && prepared.groups.size() == 2, "Frame snapshot lost identity");
    require(prepared.groups[0].beforeBarriers.size() == 1 && prepared.groups[0].afterBarriers.empty() &&
            prepared.groups[1].beforeBarriers.size() == 1 && prepared.groups[1].afterBarriers.size() == 1,
            "Preparation lost physical boundary transitions");
    require(prepared.groups[1].beforeBarriers[0].mapped.imageBarrier.oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
            prepared.groups[1].afterBarriers[0].mapped.imageBarrier.newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR &&
            prepared.groups[1].binding.imageHandle == bindings[0].imageHandle, "Wrong prepared image or layout");
    expectFailure([&] { prepareGraphFrame(plan, {}, context, 0, renderPasses); }, "missing");
    auto stale = bindings;
    stale[0].generation = 1;
    expectFailure([&] { prepareGraphFrame(plan, stale, context, 0, renderPasses); }, "stale");
    expectFailure([&] { prepareGraphFrame(plan, bindings, context, 0, {}); }, "render pass");
    const auto sampledHandle = graph.importImage({"sampled", ExternalImageRole::color, ImageUse::fragmentSampledRead, true});
    graph.addRasterPass({"sampling", {targetHandle}, {sampledHandle}});
    expectFailure([&] { prepareGraphFrame(graph.compile(), bindings, context, 0, renderPasses); }, "sampled drawing");
    expectFailure([&] { validateGraphRecordingSupport(graph.compile()); }, "sampled drawing");
    expectFailure([&] { validateGraphRecordingSupport(CompiledRenderGraph{}); }, "executable plan");
    std::vector<RecordedRenderGroup> results(2);
    for (std::size_t groupIndex = 0; groupIndex < results.size(); ++groupIndex) {
        auto& result = results[groupIndex];
        result.frameSlot = prepared.frameSlot;
        result.generation = prepared.generation;
        result.acquiredImageIndex = prepared.acquiredImageIndex;
        result.planVersion = prepared.planVersion;
        result.groupIndex = groupIndex;
        result.commandPoolHandle = reinterpret_cast<VkCommandPool>(std::uintptr_t{50 + groupIndex});
        result.commandBufferHandle = reinterpret_cast<VkCommandBuffer>(std::uintptr_t{100 + groupIndex});
        result.complete = true;
    }
    const auto ordered = collectRecordedGroupCommands(prepared, results);
    std::swap(results[0], results[1]);
    require(collectRecordedGroupCommands(prepared, results) == ordered, "Result arrival changed plan order");
    for (int damage = 0; damage < 10; ++damage) {
        auto broken = results;
        if (damage == 0) { broken.pop_back(); }
        if (damage == 1) { broken[0].groupIndex = broken[1].groupIndex; }
        if (damage == 2) { broken[0].commandBufferHandle = VK_NULL_HANDLE; }
        if (damage == 3) { broken[0].commandBufferHandle = broken[1].commandBufferHandle; }
        if (damage == 4) { broken[0].complete = false; }
        if (damage == 5) { ++broken[0].generation; }
        if (damage == 6) { ++broken[0].frameSlot; }
        if (damage == 7) { ++broken[0].planVersion; }
        if (damage == 8) { ++broken[0].acquiredImageIndex; }
        if (damage == 9) { broken[0].commandPoolHandle = VK_NULL_HANDLE; }
        expectFailure([&] { collectRecordedGroupCommands(prepared, broken); }, "record results");
    }

}
}

int main(int argumentCount, char** arguments) {
    try {
        const std::string testCase = argumentCount == 2 ? arguments[1] : "";
        if (testCase == "mapping") { mapping(); }
        else if (testCase == "bindings") { bindings(); }
        else if (testCase == "recording") { recording(); }
        else { throw std::runtime_error("Unknown Vulkan graph test case"); }
        std::cout << "PASS Vulkan graph " << testCase << " (host-only parameter fixtures)\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
