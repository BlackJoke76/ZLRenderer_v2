#include "renderGraph.h"
#include <algorithm>
#include <functional>
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
    throw std::runtime_error("Expected graph rejection did not occur.");
}
GraphImageHandle importTarget(RenderGraph& graph, const char* name = "output") {
    return graph.importImage({name, ExternalImageRole::swapchainColor});
}
void addPass(RenderGraph& graph, const char* name, GraphImageHandle colorTargetHandle,
             std::vector<GraphImageHandle> sampledImages = {}, AttachmentLoad load = AttachmentLoad::clear,
             AttachmentStore store = AttachmentStore::store) {
    graph.addRasterPass({name, {colorTargetHandle, load, store, {}}, std::move(sampledImages)});
}
bool hasDependency(const CompiledRenderGraph& plan, std::size_t imageIndex, std::size_t source,
                   std::size_t destination, DependencyReason reason) {
    return std::any_of(plan.getDependencies().begin(), plan.getDependencies().end(), [&](const auto& dependency) {
        return dependency.imageIndex == imageIndex && dependency.sourcePassIndex == source &&
               dependency.destinationPassIndex == destination && (dependency.reasons & reasonBit(reason)) != 0;
    });
}
const GraphImageTransition& findTransition(const PhysicalPassGroup& group, std::size_t imageIndex) {
    const auto found = std::find_if(group.beforeTransitions.begin(), group.beforeTransitions.end(), [&](const auto& transition) {
        return transition.imageIndex == imageIndex;
    });
    require(found != group.beforeTransitions.end(), "Expected resource transition is missing");
    return *found;
}
void declarations() {
    RenderGraph graph;
    const auto targetHandle = importTarget(graph);
    const auto sourceHandle = graph.importImage({"source"});
    addPass(graph, "source", sourceHandle);
    addPass(graph, "first", targetHandle, {sourceHandle, sourceHandle});
    addPass(graph, "second", targetHandle);
    graph.setPresentTarget(targetHandle);
    const auto plan = graph.compile();
    require(plan.getImages().size() == 2 && plan.getPasses().size() == 3, "Keep multiple resources and writes");
    require(plan.getPasses()[1].sampledImages.size() == 1, "Normalize repeated sampling declarations");
    for (std::size_t index = 0; index < 3; ++index) {
        require(plan.getGroups()[index].logicalPassIndices == std::vector<std::size_t>{index}, "One group per raster pass");
    }
    require(plan.getGroups()[0].afterTransitions.empty() && plan.getGroups()[1].afterTransitions.empty(),
            "No presentation transition between passes");
    require(plan.getGroups().back().afterTransitions.size() == 1 &&
            plan.getGroups().back().afterTransitions[0].nextUse == ImageUse::present, "Only the terminal exit presents");
    const auto secondPlan = graph.compile();
    require(plan.getDependencies() == secondPlan.getDependencies() && plan.getGroups() == secondPlan.getGroups(),
            "Compilation must be deterministic");
}
void references() {
    RenderGraph graph;
    RenderGraph otherGraph;
    const auto foreignHandle = importTarget(otherGraph);
    expectFailure([&] { addPass(graph, "foreign", foreignHandle); }, "reference");
    expectFailure([&] { addPass(graph, "invalid", {}); }, "reference");
    expectFailure([&] { graph.setPresentTarget(foreignHandle); }, "reference");
    const auto targetHandle = importTarget(graph);
    expectFailure([&] { addPass(graph, "feedback", targetHandle, {targetHandle}); }, "feedback");
    expectFailure([&] { addPass(graph, "bad-load", targetHandle, {}, static_cast<AttachmentLoad>(99)); }, "attachment");
    expectFailure([&] { addPass(graph, "bad-store", targetHandle, {}, AttachmentLoad::clear,
                              static_cast<AttachmentStore>(99)); }, "attachment");
}
void unsupportedShapes() {
    for (unsigned field = 0; field < 3; ++field) {
        RenderGraph graph;
        ExternalImageDescription image{"unsupported"};
        if (field == 0) { image.mipLevels = 2; }
        if (field == 1) { image.arrayLayers = 2; }
        if (field == 2) { image.sampleCount = 4; }
        expectFailure([&] { (void)graph.importImage(image); }, "single-mip/layer/sample");
    }
    RenderGraph graph;
    expectFailure([&] { (void)graph.importImage({"bad-entry", ExternalImageRole::color, ImageUse::undefined, true}); },
                  "external state");
    expectFailure([&] { (void)graph.importImage({"bad-present", ExternalImageRole::color, ImageUse::present, true}); },
                  "external state");
}
void exits() {
    RenderGraph graph;
    const auto targetHandle = importTarget(graph);
    expectFailure([&] { (void)graph.compile(); }, "work and a presentation exit");
    addPass(graph, "clear", targetHandle);
    expectFailure([&] { (void)graph.compile(); }, "presentation exit");
    const auto ordinaryHandle = graph.importImage({"ordinary"});
    expectFailure([&] { graph.setPresentTarget(ordinaryHandle); }, "non-presentable");
    graph.setPresentTarget(targetHandle);
    expectFailure([&] { graph.setPresentTarget(targetHandle); }, "duplicate");
    (void)graph.compile();
}
void rawAndLoad() {
    RenderGraph graph;
    const auto sourceHandle = graph.importImage({"source"});
    const auto targetHandle = importTarget(graph);
    addPass(graph, "producer", sourceHandle);
    addPass(graph, "sample", targetHandle, {sourceHandle});
    addPass(graph, "load", targetHandle, {}, AttachmentLoad::load);
    graph.setPresentTarget(targetHandle);
    const auto plan = graph.compile();
    require(hasDependency(plan, sourceHandle.getIndex(), 0, 1, DependencyReason::readAfterWrite), "Missing RAW producer");
    const auto& transition = findTransition(plan.getGroups()[1], sourceHandle.getIndex());
    require(transition.previousUse == ImageUse::colorAttachmentWrite && transition.nextUse == ImageUse::fragmentSampledRead &&
            (transition.reasons & reasonBit(DependencyReason::layoutTransition)) != 0, "Missing write-to-sample transition");
    require(hasDependency(plan, targetHandle.getIndex(), 1, 2, DependencyReason::readAfterWrite), "LOAD reads previous content");
    require(hasDependency(plan, targetHandle.getIndex(), 1, 2, DependencyReason::writeAfterWrite), "LOAD also writes attachment");
}
void war() {
    RenderGraph graph;
    const auto sourceHandle = graph.importImage({"source", ExternalImageRole::color, ImageUse::fragmentSampledRead, true});
    const auto targetHandle = importTarget(graph);
    addPass(graph, "reader", targetHandle, {sourceHandle});
    addPass(graph, "overwrite", sourceHandle);
    graph.setPresentTarget(targetHandle);
    const auto plan = graph.compile();
    require(hasDependency(plan, sourceHandle.getIndex(), 0, 1, DependencyReason::writeAfterRead), "Missing WAR reader");
    const auto& transition = findTransition(plan.getGroups()[1], sourceHandle.getIndex());
    require(transition.sourceUses == useBit(ImageUse::fragmentSampledRead), "WAR must cover the read stage");
    require((transition.reasons & reasonBit(DependencyReason::writeAfterWrite)) == 0, "Do not invent a previous writer");
}
void waw() {
    RenderGraph graph;
    const auto targetHandle = importTarget(graph);
    addPass(graph, "clearA", targetHandle);
    addPass(graph, "clearB", targetHandle);
    graph.setPresentTarget(targetHandle);
    const auto plan = graph.compile();
    require(hasDependency(plan, 0, 0, 1, DependencyReason::writeAfterWrite), "Clearing again still needs WAW");
    const auto& transition = findTransition(plan.getGroups()[1], 0);
    require(transition.previousUse == ImageUse::colorAttachmentWrite && transition.nextUse == transition.previousUse,
            "Second clear must preserve tracked layout rather than restart from undefined");
    require((transition.reasons & reasonBit(DependencyReason::layoutTransition)) == 0, "Same-layout WAW is not a transition");
}
void allReaders() {
    RenderGraph graph;
    const auto sourceHandle = importTarget(graph, "shared");
    const auto firstTargetHandle = graph.importImage({"firstOutput"});
    const auto secondTargetHandle = graph.importImage({"secondOutput"});
    addPass(graph, "writer", sourceHandle);
    addPass(graph, "readerA", firstTargetHandle, {sourceHandle});
    addPass(graph, "readerB", secondTargetHandle, {sourceHandle});
    addPass(graph, "overwrite", sourceHandle);
    graph.setPresentTarget(sourceHandle);
    const auto plan = graph.compile();
    require(hasDependency(plan, 0, 1, 3, DependencyReason::writeAfterRead), "Do not lose the first reader");
    require(hasDependency(plan, 0, 2, 3, DependencyReason::writeAfterRead), "Do not lose the last reader");
    require(hasDependency(plan, 0, 0, 3, DependencyReason::writeAfterWrite), "Preserve logical writer dependency");
    require(std::none_of(plan.getGroups()[2].beforeTransitions.begin(), plan.getGroups()[2].beforeTransitions.end(),
                        [](const auto& transition) { return transition.imageIndex == 0; }), "Compatible reads need no new barrier");
    const auto& transition = findTransition(plan.getGroups()[3], 0);
    require((transition.sourceUses & useBit(ImageUse::fragmentSampledRead)) != 0 &&
            (transition.sourceUses & useBit(ImageUse::colorAttachmentWrite)) != 0, "Cover readers and previous writer scopes");
    require(std::none_of(plan.getDependencies().begin(), plan.getDependencies().end(), [](const auto& dependency) {
        return dependency.sourcePassIndex == dependency.destinationPassIndex;
    }), "No self dependencies");
}
void undefinedContents() {
    for (const auto loadInstead : {false, true}) {
        RenderGraph graph;
        const auto sourceHandle = graph.importImage({"undefinedSource"});
        const auto targetHandle = importTarget(graph);
        if (loadInstead) {
            addPass(graph, "validOutput", targetHandle);
            addPass(graph, "badLoad", sourceHandle, {}, AttachmentLoad::load);
        }
        else { addPass(graph, "badSample", targetHandle, {sourceHandle}); }
        graph.setPresentTarget(targetHandle);
        expectFailure([&] { (void)graph.compile(); }, "undefined contents");
    }
    RenderGraph graph;
    const auto targetHandle = importTarget(graph);
    addPass(graph, "discard", targetHandle, {}, AttachmentLoad::clear, AttachmentStore::discard);
    graph.setPresentTarget(targetHandle);
    expectFailure([&] { (void)graph.compile(); }, "undefined contents");
    addPass(graph, "reinitialize", targetHandle);
    (void)graph.compile();
    RenderGraph discardedLoadGraph;
    const auto discardedTargetHandle = importTarget(discardedLoadGraph);
    addPass(discardedLoadGraph, "discard", discardedTargetHandle, {}, AttachmentLoad::clear, AttachmentStore::discard);
    addPass(discardedLoadGraph, "invalidPreserve", discardedTargetHandle, {}, AttachmentLoad::load);
    discardedLoadGraph.setPresentTarget(discardedTargetHandle);
    expectFailure([&] { (void)discardedLoadGraph.compile(); }, "invalidPreserve");
}
}

int main(int argumentCount, char** arguments) {
    try {
        const std::string testCase = argumentCount == 2 ? arguments[1] : "";
        const std::pair<const char*, std::function<void()>> cases[]{
            {"declarations", declarations}, {"references", references}, {"shapes", unsupportedShapes}, {"exits", exits},
            {"raw", rawAndLoad}, {"war", war}, {"waw", waw}, {"readers", allReaders}, {"contents", undefinedContents}};
        bool found = false;
        for (const auto& [name, operation] : cases) {
            if (testCase.empty() || testCase == name) { operation(); found = true; std::cout << "PASS graph " << name << '\n'; }
        }
        require(found, "Unknown graph test case");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
