#include "rhi.h"
#include "vulkanContext.h"
#include "vulkanPresenter.h"
#include <array>
#include <cmath>
#include <stdexcept>

class VulkanBackend {
public:
    VulkanContext context;
    VulkanPresenter presenter; // Released before the context it borrows.
};

Rhi::Rhi() = default;
Rhi::~Rhi() noexcept = default;

void Rhi::initialize(GLFWwindow* windowHandle, const RhiConfig& config) {
    if (!windowHandle || backend) { throw std::runtime_error("RHI requires a window and an empty renderer."); }
    for (const auto component : std::array{config.clearColor.red, config.clearColor.green,
                                           config.clearColor.blue, config.clearColor.alpha}) {
        if (!std::isfinite(component) || component < 0.0f || component > 1.0f) {
            throw std::runtime_error("RHI clear color must contain finite components in [0,1].");
        }
    }
    backend = std::make_unique<VulkanBackend>();
    backend->context.initialize(windowHandle);
    backend->presenter.initialize(windowHandle, backend->context.getDeviceView(), config.clearColor);
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
