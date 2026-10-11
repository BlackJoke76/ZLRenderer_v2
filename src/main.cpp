#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "vulkanContext.h"

#include <cstdlib>
#include <cstdio>
#include <exception>
#include <memory>

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

        VulkanContext vulkanContext;
        vulkanContext.initialize(windowHandle.get());
        while (glfwWindowShouldClose(windowHandle.get()) == GLFW_FALSE) {
            glfwWaitEvents();
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Initialization failed: %s\n", error.what());
        return EXIT_FAILURE;
    }
}
