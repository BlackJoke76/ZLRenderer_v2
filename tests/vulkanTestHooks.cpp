#include "vulkanTestHooks.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

void runSwapchainTestCheckpoint(const char* stage, std::uint32_t generation) {
    runVulkanTestCheckpoint(stage, VK_NULL_HANDLE);
    if (generation > 1 && isVulkanTestCase((std::string("rebuild-") + stage).c_str())) {
        throw std::runtime_error(std::string("rebuild-") + stage + ": injected failure after successful creation.");
    }
}

bool shouldRequestSwapchainTestRebuild(std::uint64_t submittedFrames, std::uint32_t generation) {
    if (isVulkanTestCase("old-chain")) {
        return (submittedFrames == 3 && generation == 1) || (submittedFrames == 6 && generation == 2);
    }
    if (submittedFrames != 3 || generation != 1) { return false; }
    for (const auto caseName : {"rebuild-failure", "rebuild-create", "rebuild-render-pass", "rebuild-image-view",
                               "rebuild-framebuffer", "rebuild-present-semaphore", "prepare-resize", "prepare-close",
                               "prepare-zero", "prepare-iconified", "surface-zero", "surface-variable-zero", "surface-width-zero"}) {
        if (isVulkanTestCase(caseName)) { return true; }
    }
    return false;
}

bool isVulkanTestCase(const char* caseName) {
#ifdef _MSC_VER
    char requestedCase[64]{};
    std::size_t requiredSize = 0;
    if (getenv_s(&requiredSize, requestedCase, sizeof(requestedCase), "ZL_VULKAN_TEST_CASE") != 0) {
        throw std::runtime_error("Invalid lifecycle test case environment.");
    }
    return requiredSize != 0 && std::strcmp(requestedCase, caseName) == 0;
#else
    const auto requestedCase = std::getenv("ZL_VULKAN_TEST_CASE");
    return requestedCase && std::strcmp(requestedCase, caseName) == 0;
#endif
}

void runVulkanTestCheckpoint(const char* stage, VkInstance instanceHandle) {
    if (isVulkanTestCase(stage)) {
        throw std::runtime_error(std::string(stage) + ": injected failure after successful creation.");
    }
    if (std::strcmp(stage, "messenger") == 0 && isVulkanTestCase("diagnostic")) {
        const auto submitMessage = reinterpret_cast<PFN_vkSubmitDebugUtilsMessageEXT>(
            vkGetInstanceProcAddr(instanceHandle, "vkSubmitDebugUtilsMessageEXT"));
        if (!submitMessage) {
            throw std::runtime_error("diagnostic: submit-message entry point unavailable.");
        }
        VkDebugUtilsMessengerCallbackDataEXT message{};
        message.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CALLBACK_DATA_EXT;
        message.pMessageIdName = "ZLRenderer-ControlledDiagnostic";
        message.pMessage = "Controlled callback delivery test; no invalid Vulkan operation.";
        submitMessage(instanceHandle, VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
                      VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT, &message);
    }
}

void runPresentationPreparationTest(GLFWwindow* windowHandle, std::uint64_t submittedFrames) {
    static bool changedSize = false;
    if (submittedFrames == 3 && !changedSize && isVulkanTestCase("prepare-resize")) {
        changedSize = true;
        glfwSetWindowSize(windowHandle, 640, 480);
        std::puts("Controlled preparation resize: 640x480");
    }
}

void applyWindowStateTest(const char* stage, GLFWwindow* windowHandle, std::uint64_t submittedFrames,
    int& framebufferWidth, int& framebufferHeight, bool& iconified, bool& closing) {
    static unsigned pauseReads = 0;
    const auto initial = std::strcmp(stage, "initialize") == 0;
    const auto preparing = std::strcmp(stage, "rebuild") == 0;
    if (initial && (isVulkanTestCase("initial-zero") || isVulkanTestCase("initial-zero-close"))) {
        framebufferWidth = 0;
        framebufferHeight = 0;
        std::puts("Controlled initial zero framebuffer");
    }
    if (initial && isVulkanTestCase("initial-iconified")) {
        iconified = true;
        std::puts("Controlled initial iconified with nonzero framebuffer");
    }
    if ((preparing && submittedFrames == 3 && isVulkanTestCase("prepare-close")) ||
        (!initial && isVulkanTestCase("initial-zero-close"))) {
        closing = true;
        glfwSetWindowShouldClose(windowHandle, GLFW_TRUE);
        std::printf("Controlled close before creation: submitted=%llu\n", static_cast<unsigned long long>(submittedFrames));
    }
    if (submittedFrames == 3 && (isVulkanTestCase("prepare-zero") || isVulkanTestCase("prepare-iconified")) &&
        (preparing || pauseReads > 0) && pauseReads < 3) {
        ++pauseReads;
        if (isVulkanTestCase("prepare-zero")) { framebufferWidth = 0; framebufferHeight = 0; }
        else { iconified = true; }
        std::printf("Controlled pause read=%u submitted=%llu\n", pauseReads, static_cast<unsigned long long>(submittedFrames));
    }
}

void applySurfaceExtentTest(VkSurfaceCapabilitiesKHR& capabilities, std::uint64_t submittedFrames) {
    static unsigned zeroQueries = 0;
    if (submittedFrames != 3 || zeroQueries >= 3) { return; }
    if (isVulkanTestCase("surface-zero")) { capabilities.currentExtent = {0, 0}; }
    else if (isVulkanTestCase("surface-variable-zero")) {
        capabilities.currentExtent = {UINT32_MAX, UINT32_MAX};
        capabilities.minImageExtent = {0, 0};
        capabilities.maxImageExtent = {0, 0};
    } else if (isVulkanTestCase("surface-width-zero")) { capabilities.currentExtent = {0, 480}; }
    else { return; }
    ++zeroQueries;
    std::printf("Controlled surface zero query=%u submitted=%llu\n", zeroQueries, static_cast<unsigned long long>(submittedFrames));
}
