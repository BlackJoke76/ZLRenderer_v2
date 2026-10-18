#include "rhi.h"
#include "vulkanContext.h"
#include "vulkanPresenter.h"
#include <stdexcept>
#ifdef ZL_RENDERER_TESTING
#include "vulkanTestHooks.h"
#endif

class VulkanBackend {
public:
    RenderGraph graph;
    CompiledRenderGraph compiledGraph;
    VulkanContext context;
    VulkanPresenter presenter; // Released before the context it borrows.
};

Rhi::Rhi() = default;
Rhi::~Rhi() noexcept = default;

void Rhi::initialize(GLFWwindow* windowHandle, const RhiConfig& config) {
    if (!windowHandle || backend) { throw std::runtime_error("RHI requires a window and an empty renderer."); }
    validateClearColor(config.clearColor);
    backend = std::make_unique<VulkanBackend>();
    const auto colorTargetHandle = backend->graph.importImage({"swapchainColor", ExternalImageRole::swapchainColor});
    std::vector<GraphImageHandle> sampledImages;
#ifdef ZL_RENDERER_TESTING
    if (isVulkanTestCase("graph-unsupported")) {
        sampledImages.push_back(backend->graph.importImage({"unsupportedSample", ExternalImageRole::color,
                                                          ImageUse::fragmentSampledRead, true}));
    }
    if (isVulkanTestCase("graph-double") || isVulkanTestCase("graph-double-window") || isVulkanTestCase("graph-group")) {
        backend->graph.addRasterPass({"clearA", {colorTargetHandle, AttachmentLoad::clear, AttachmentStore::store,
                                                {0.20f, 0.05f, 0.10f, 1.0f}}, {}});
    }
#endif
    backend->graph.addRasterPass({"clearColor", {colorTargetHandle, AttachmentLoad::clear, AttachmentStore::store, config.clearColor},
                                  std::move(sampledImages)});
    backend->graph.setPresentTarget(colorTargetHandle);
    backend->compiledGraph = backend->graph.compile();
    backend->context.initialize(windowHandle);
    backend->presenter.initialize(windowHandle, backend->context.getDeviceView(), backend->compiledGraph);
}

void Rhi::renderFrame() {
    if (!backend) { throw std::runtime_error("RHI renderFrame requires initialization."); }
    backend->presenter.renderFrame();
}

void Rhi::shutdown() {
    if (backend) {
        backend->presenter.shutdown();
        backend.reset();
    }
}
