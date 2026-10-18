#include "renderGraph.h"
#include <algorithm>
#include <atomic>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {
bool writesImage(ImageUse use) {
    return use == ImageUse::colorAttachmentWrite || use == ImageUse::colorAttachmentReadWrite;
}
bool readsImage(ImageUse use) {
    return use == ImageUse::fragmentSampledRead || use == ImageUse::colorAttachmentReadWrite || use == ImageUse::present;
}
bool sameLayout(ImageUse firstUse, ImageUse secondUse) {
    return firstUse == secondUse || (writesImage(firstUse) && writesImage(secondUse));
}
struct ImageAccess {
    std::size_t passIndex;
    ImageUse use;
};
struct ImageHistory {
    ImageUse currentUse;
    bool contentsDefined;
    bool firstUse = true;
    std::optional<ImageAccess> writer;
    std::vector<ImageAccess> readers;
};
}

const char* getImageUseName(ImageUse use) {
    switch (use) {
    case ImageUse::undefined: return "undefined";
    case ImageUse::colorAttachmentWrite: return "colorAttachmentWrite";
    case ImageUse::colorAttachmentReadWrite: return "colorAttachmentReadWrite";
    case ImageUse::fragmentSampledRead: return "fragmentSampledRead";
    case ImageUse::present: return "present";
    }
    throw std::runtime_error("graph: invalid image use.");
}

std::string describeDependencyReasons(std::uint32_t reasons) {
    std::string description;
    for (const auto& [reason, name] : {std::pair{DependencyReason::readAfterWrite, "RAW"},
             {DependencyReason::writeAfterRead, "WAR"}, {DependencyReason::writeAfterWrite, "WAW"},
             {DependencyReason::layoutTransition, "layout"}, {DependencyReason::external, "external"},
             {DependencyReason::presentation, "present"}}) {
        if ((reasons & reasonBit(reason)) == 0) { continue; }
        if (!description.empty()) { description += '|'; }
        description += name;
    }
    return description;
}

RenderGraph::RenderGraph() {
    static std::atomic<std::uint64_t> nextIdentity{1};
    graphIdentity = nextIdentity.fetch_add(1, std::memory_order_relaxed);
}

void RenderGraph::validateHandle(GraphImageHandle imageHandle) const {
    if (imageHandle.getGraphIdentity() != graphIdentity || imageHandle.getIndex() >= images.size()) {
        throw std::runtime_error("graph: invalid or foreign image reference.");
    }
}

GraphImageHandle RenderGraph::importImage(ExternalImageDescription description) {
    (void)getImageUseName(description.initialUse);
    if (description.name.empty() || description.mipLevels != 1 || description.arrayLayers != 1 ||
        description.sampleCount != 1 ||
        (description.role != ExternalImageRole::color && description.role != ExternalImageRole::swapchainColor)) {
        throw std::runtime_error("graph import " + description.name + ": requires a named single-mip/layer/sample color image.");
    }
    if ((description.contentsDefined && description.initialUse == ImageUse::undefined) ||
        (description.initialUse == ImageUse::present && description.role != ExternalImageRole::swapchainColor)) {
        throw std::runtime_error("graph import " + description.name + ": inconsistent external state.");
    }
    const auto imageIndex = images.size();
    images.push_back(std::move(description));
    return {graphIdentity, imageIndex};
}

void RenderGraph::addRasterPass(RasterPassDeclaration declaration) {
    validateHandle(declaration.colorAttachment.colorTargetHandle);
    const auto& attachment = declaration.colorAttachment;
    if (declaration.name.empty() ||
        (attachment.load != AttachmentLoad::clear && attachment.load != AttachmentLoad::load) ||
        (attachment.store != AttachmentStore::store && attachment.store != AttachmentStore::discard)) {
        throw std::runtime_error("graph pass " + declaration.name + ": invalid attachment declaration.");
    }
    validateClearColor(attachment.clearColor);
    std::vector<GraphImageHandle> sampledImages;
    for (const auto sampledImageHandle : declaration.sampledImages) {
        validateHandle(sampledImageHandle);
        if (sampledImageHandle == attachment.colorTargetHandle) {
            throw std::runtime_error("graph pass " + declaration.name + ": attachment sampling feedback is unsupported.");
        }
        if (std::find(sampledImages.begin(), sampledImages.end(), sampledImageHandle) == sampledImages.end()) {
            sampledImages.push_back(sampledImageHandle);
        }
    }
    declaration.sampledImages = std::move(sampledImages);
    passes.push_back(std::move(declaration));
}

void RenderGraph::setPresentTarget(GraphImageHandle colorTargetHandle) {
    validateHandle(colorTargetHandle);
    if (hasPresentTarget || images[colorTargetHandle.getIndex()].role != ExternalImageRole::swapchainColor) {
        throw std::runtime_error("graph: duplicate or non-presentable presentation exit.");
    }
    presentTargetHandle = colorTargetHandle;
    hasPresentTarget = true;
}

CompiledRenderGraph RenderGraph::compile() const {
    if (passes.empty() || !hasPresentTarget) { throw std::runtime_error("graph compile: work and a presentation exit are required."); }
    CompiledRenderGraph result;
    static std::atomic<std::uint64_t> nextVersion{1};
    result.version = nextVersion.fetch_add(1, std::memory_order_relaxed);
    result.graphIdentity = graphIdentity;
    result.images = images;
    result.passes = passes;
    result.presentImageIndex = presentTargetHandle.getIndex();
    std::vector<ImageHistory> histories;
    histories.reserve(images.size());
    for (const auto& image : images) {
        ImageHistory history{image.initialUse, image.contentsDefined, true, {}, {}};
        if (writesImage(image.initialUse)) { history.writer = ImageAccess{externalPassIndex, image.initialUse}; }
        else if (readsImage(image.initialUse) && image.initialUse != ImageUse::present) {
            history.readers.push_back({externalPassIndex, image.initialUse});
        }
        histories.push_back(std::move(history));
    }

    // Keep logical edges even when an earlier boundary already made a writer visible.
    const auto addDependency = [&](std::size_t imageIndex, std::size_t sourcePass,
                                   std::size_t destinationPass, std::uint32_t reasons) {
        if (sourcePass == destinationPass) { return; }
        const auto found = std::find_if(result.dependencies.begin(), result.dependencies.end(), [&](const auto& dependency) {
            return dependency.imageIndex == imageIndex && dependency.sourcePassIndex == sourcePass &&
                   dependency.destinationPassIndex == destinationPass;
        });
        if (found != result.dependencies.end()) { found->reasons |= reasons; }
        else { result.dependencies.push_back({imageIndex, sourcePass, destinationPass, reasons}); }
    };
    const auto analyzeUse = [&](std::size_t imageIndex, std::size_t passIndex, ImageUse nextUse,
                                std::vector<GraphImageTransition>& transitions) {
        auto& history = histories[imageIndex];
        const auto reading = readsImage(nextUse);
        const auto writing = writesImage(nextUse);
        const auto layoutChanged = !sameLayout(history.currentUse, nextUse);
        if (reading && !history.contentsDefined) {
            const auto operation = passIndex == passes.size() ? std::string("present") : passes[passIndex].name;
            throw std::runtime_error("graph compile " + operation + ": undefined contents of " + images[imageIndex].name);
        }
        std::uint32_t reasons = 0;
        std::uint32_t sourceUses = 0;
        const auto dependOn = [&](ImageAccess source, std::uint32_t edgeReasons) {
            if (layoutChanged) { edgeReasons |= reasonBit(DependencyReason::layoutTransition); }
            if (nextUse == ImageUse::present) { edgeReasons |= reasonBit(DependencyReason::presentation); }
            addDependency(imageIndex, source.passIndex, passIndex, edgeReasons);
            reasons |= edgeReasons;
            sourceUses |= useBit(source.use);
        };
        if (history.firstUse) {
            dependOn({externalPassIndex, history.currentUse}, reasonBit(DependencyReason::external));
        }
        if (history.writer) {
            std::uint32_t writerReasons = 0;
            if (reading) { writerReasons |= reasonBit(DependencyReason::readAfterWrite); }
            if (writing) { writerReasons |= reasonBit(DependencyReason::writeAfterWrite); }
            if (writerReasons != 0) { dependOn(*history.writer, writerReasons); }
        }
        if (writing || layoutChanged) {
            for (const auto reader : history.readers) {
                dependOn(reader, writing ? reasonBit(DependencyReason::writeAfterRead) : 0);
            }
        }
        // Compatible reads reuse the previous visibility operation. A write always needs ordering.
        if (history.firstUse || writing || layoutChanged || writesImage(history.currentUse)) {
            if (sourceUses == 0) { sourceUses = useBit(history.currentUse); }
            transitions.push_back({imageIndex, history.currentUse, nextUse, sourceUses, reasons});
        }
        if (writing) {
            history.writer = ImageAccess{passIndex, nextUse};
            history.readers.clear();
        } else if (reading && nextUse != ImageUse::present) {
            history.readers.push_back({passIndex, nextUse});
        }
        history.currentUse = nextUse;
        history.firstUse = false;
    };

    for (std::size_t passIndex = 0; passIndex < passes.size(); ++passIndex) {
        const auto& pass = passes[passIndex];
        PhysicalPassGroup group{{passIndex}, {}, {}};
        for (const auto sampledImageHandle : pass.sampledImages) {
            analyzeUse(sampledImageHandle.getIndex(), passIndex, ImageUse::fragmentSampledRead, group.beforeTransitions);
        }
        const auto& attachment = pass.colorAttachment;
        const auto imageIndex = attachment.colorTargetHandle.getIndex();
        const auto nextUse = attachment.load == AttachmentLoad::load ?
            ImageUse::colorAttachmentReadWrite : ImageUse::colorAttachmentWrite;
        analyzeUse(imageIndex, passIndex, nextUse, group.beforeTransitions);
        histories[imageIndex].contentsDefined = attachment.store == AttachmentStore::store;
        result.groups.push_back(std::move(group));
    }
    analyzeUse(result.presentImageIndex, passes.size(), ImageUse::present, result.groups.back().afterTransitions);
    return result;
}
