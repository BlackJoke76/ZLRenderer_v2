#pragma once
#include <memory>

struct GLFWwindow;
struct ClearColor {
    float red = 0.05f;
    float green = 0.10f;
    float blue = 0.20f;
    float alpha = 1.0f;
};
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
