#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
void reportGlfwError(int errorCode, const char* description) {
    std::cerr << "GLFW error " << errorCode << ": " << description << '\n';
}

struct GlfwLifetime {
    ~GlfwLifetime() { glfwTerminate(); }
};
}

int main() {
    glfwSetErrorCallback(reportGlfwError);
    if (glfwInit() != GLFW_TRUE) {
        std::cerr << "Failed to initialize GLFW.\n";
        return EXIT_FAILURE;
    }
    const auto glfwLifetime = GlfwLifetime{};

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    const auto windowHandle = std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)>(
        glfwCreateWindow(1280, 720, "ZLRenderer", nullptr, nullptr), glfwDestroyWindow);
    if (!windowHandle) {
        std::cerr << "Failed to create the window.\n";
        return EXIT_FAILURE;
    }

    while (glfwWindowShouldClose(windowHandle.get()) == GLFW_FALSE) {
        glfwWaitEvents();
    }
    return EXIT_SUCCESS;
}
