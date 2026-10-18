#pragma once
#include "clearColor.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

enum class ImageUse { undefined, colorAttachmentWrite, colorAttachmentReadWrite, fragmentSampledRead, present };
enum class ExternalImageRole { color, swapchainColor };
enum class AttachmentLoad { clear, load };
enum class AttachmentStore { store, discard };

class RenderGraph;
class GraphImageHandle {
public:
    GraphImageHandle() = default;
    bool operator==(const GraphImageHandle&) const = default;
    std::size_t getIndex() const { return imageIndex; }
    std::uint64_t getGraphIdentity() const { return graphIdentity; }
private:
    friend class RenderGraph;
    friend class CompiledRenderGraph;
    GraphImageHandle(std::uint64_t identity, std::size_t index) : graphIdentity(identity), imageIndex(index) {}
    std::uint64_t graphIdentity = 0;
    std::size_t imageIndex = std::numeric_limits<std::size_t>::max();
};

struct ExternalImageDescription {
    std::string name;
    ExternalImageRole role = ExternalImageRole::color;
    ImageUse initialUse = ImageUse::undefined;
    bool contentsDefined = false;
    std::uint32_t mipLevels = 1;
    std::uint32_t arrayLayers = 1;
    std::uint32_t sampleCount = 1;
    bool operator==(const ExternalImageDescription&) const = default;
};
struct ColorAttachmentDeclaration {
    GraphImageHandle colorTargetHandle;
    AttachmentLoad load = AttachmentLoad::clear;
    AttachmentStore store = AttachmentStore::store;
    ClearColor clearColor{};
};
struct RasterPassDeclaration {
    std::string name;
    ColorAttachmentDeclaration colorAttachment;
    std::vector<GraphImageHandle> sampledImages;
};

inline constexpr std::size_t externalPassIndex = std::numeric_limits<std::size_t>::max();
enum class DependencyReason : std::uint32_t {
    readAfterWrite = 1, writeAfterRead = 2, writeAfterWrite = 4,
    layoutTransition = 8, external = 16, presentation = 32
};
constexpr std::uint32_t reasonBit(DependencyReason reason) { return static_cast<std::uint32_t>(reason); }
constexpr std::uint32_t useBit(ImageUse use) { return 1u << static_cast<unsigned>(use); }
struct GraphDependency {
    std::size_t imageIndex;
    std::size_t sourcePassIndex;
    std::size_t destinationPassIndex; // pass count denotes the presentation exit.
    std::uint32_t reasons;
    bool operator==(const GraphDependency&) const = default;
};
struct GraphImageTransition {
    std::size_t imageIndex;
    ImageUse previousUse;
    ImageUse nextUse;
    std::uint32_t sourceUses;
    std::uint32_t reasons;
    bool operator==(const GraphImageTransition&) const = default;
};
struct PhysicalPassGroup {
    std::vector<std::size_t> logicalPassIndices;
    std::vector<GraphImageTransition> beforeTransitions;
    std::vector<GraphImageTransition> afterTransitions;
    bool operator==(const PhysicalPassGroup&) const = default;
};

class CompiledRenderGraph {
public:
    const auto& getImages() const { return images; }
    const auto& getPasses() const { return passes; }
    const auto& getDependencies() const { return dependencies; }
    const auto& getGroups() const { return groups; }
    std::size_t getPresentImageIndex() const { return presentImageIndex; }
    std::uint64_t getIdentity() const { return graphIdentity; }
    std::uint64_t getVersion() const { return version; }
    GraphImageHandle getPresentTargetHandle() const { return {graphIdentity, presentImageIndex}; }
private:
    friend class RenderGraph;
    std::uint64_t graphIdentity = 0;
    std::uint64_t version = 0;
    std::vector<ExternalImageDescription> images;
    std::vector<RasterPassDeclaration> passes;
    std::vector<GraphDependency> dependencies;
    std::vector<PhysicalPassGroup> groups;
    std::size_t presentImageIndex = externalPassIndex;
};

class RenderGraph {
public:
    RenderGraph();
    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;
    GraphImageHandle importImage(ExternalImageDescription description);
    void addRasterPass(RasterPassDeclaration declaration);
    void setPresentTarget(GraphImageHandle colorTargetHandle);
    CompiledRenderGraph compile() const;
private:
    void validateHandle(GraphImageHandle imageHandle) const;
    std::uint64_t graphIdentity;
    std::vector<ExternalImageDescription> images;
    std::vector<RasterPassDeclaration> passes;
    GraphImageHandle presentTargetHandle;
    bool hasPresentTarget = false;
};

const char* getImageUseName(ImageUse use);
std::string describeDependencyReasons(std::uint32_t reasons);
