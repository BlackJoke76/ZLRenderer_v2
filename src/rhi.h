#pragma once
#include "clearColor.h"
#include <memory>

struct GLFWwindow;
struct RhiConfig {
    ClearColor clearColor{};
};
class VulkanBackend;

class Rhi {
public:
    Rhi();
    ~Rhi() noexcept;
    Rhi(const Rhi&) = delete;
    Rhi& operator=(const Rhi&) = delete;
    Rhi(Rhi&&) = delete;
    Rhi& operator=(Rhi&&) = delete;
    void initialize(GLFWwindow* windowHandle, const RhiConfig& config = {});
    void renderFrame();
    void shutdown();
private:
    std::unique_ptr<VulkanBackend> backend;
};
