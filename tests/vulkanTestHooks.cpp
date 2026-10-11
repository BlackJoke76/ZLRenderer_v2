#include "vulkanTestHooks.h"

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

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
