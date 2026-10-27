#pragma once
#include "clearColor.h"
#include <memory>
#include <cstddef>

struct GLFWwindow;
struct RhiConfig {
    ClearColor clearColor{};
    std::size_t recordingWorkerCount = 0;
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
