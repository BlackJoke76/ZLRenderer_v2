#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "rhi.h"

#include <cstdlib>
#include <cstdio>
#include <exception>
#include <memory>
#ifdef ZL_RENDERER_TESTING
#include "vulkanTestHooks.h"
#include <limits>
#endif

namespace {
void reportGlfwError(int errorCode, const char* description) noexcept {
    std::fprintf(stderr, "GLFW error %d: %s\n", errorCode, description);
}

struct GlfwLifetime {
    ~GlfwLifetime() noexcept {
        glfwTerminate();
        std::puts("Terminated GLFW");
    }
};
void destroyWindow(GLFWwindow* windowHandle) noexcept {
    glfwDestroyWindow(windowHandle);
    std::puts("Destroyed window");
}
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        glfwSetErrorCallback(reportGlfwError);
        if (glfwInit() != GLFW_TRUE) {
            std::fprintf(stderr, "Failed to initialize GLFW.\n");
            return EXIT_FAILURE;
        }
        const auto glfwLifetime = GlfwLifetime{};

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        const auto windowHandle = std::unique_ptr<GLFWwindow, decltype(&destroyWindow)>(
            glfwCreateWindow(1280, 720, "ZLRenderer", nullptr, nullptr), destroyWindow);
        if (!windowHandle) {
            std::fprintf(stderr, "Failed to create the window.\n");
            return EXIT_FAILURE;
        }

        RhiConfig rendererConfig;
#ifdef ZL_RENDERER_TESTING
        if (isVulkanTestCase("alternate-window") || isVulkanTestCase("alternate")) {
            rendererConfig.clearColor = {0.2f, 0.05f, 0.1f, 1.0f};
        }
        if (isVulkanTestCase("invalid-color")) { rendererConfig.clearColor.red = -1.0f; }
        if (isVulkanTestCase("nonfinite-color")) { rendererConfig.clearColor.blue = std::numeric_limits<float>::quiet_NaN(); }
#endif
        Rhi renderer;
        renderer.initialize(windowHandle.get(), rendererConfig);
        while (glfwWindowShouldClose(windowHandle.get()) == GLFW_FALSE) {
            glfwPollEvents();
            if (glfwWindowShouldClose(windowHandle.get()) == GLFW_TRUE) { break; }
            renderer.renderFrame();
            if (glfwWindowShouldClose(windowHandle.get()) == GLFW_TRUE) { break; }
            int framebufferWidth = 0;
            int framebufferHeight = 0;
            glfwGetFramebufferSize(windowHandle.get(), &framebufferWidth, &framebufferHeight);
            if (framebufferWidth == 0 || framebufferHeight == 0 || glfwGetWindowAttrib(windowHandle.get(), GLFW_ICONIFIED) == GLFW_TRUE) {
                glfwWaitEvents();
                continue;
            }
        }
        renderer.shutdown();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Renderer failed: %s\n", error.what());
        return EXIT_FAILURE;
    }
}
