#include "vulkanContext.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <concepts>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef ZL_RENDERER_TESTING
#include "vulkanTestHooks.h"
#endif

namespace {
constexpr const char* validationLayerName = "VK_LAYER_KHRONOS_validation";

void checkResult(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed (VkResult=" +
                                 std::to_string(result) + ").");
    }
}

template <typename Property, typename Enumerate>
    requires std::invocable<Enumerate, std::uint32_t*, Property*>
std::vector<Property> enumerateProperties(Enumerate enumerate, const char* operation) {
    // A changing driver/layer list can outgrow the count between these two calls.
    for (;;) {
        std::uint32_t propertyCount = 0;
        const auto countResult = enumerate(&propertyCount, static_cast<Property*>(nullptr));
        if (countResult == VK_INCOMPLETE) {
            continue;
        }
        checkResult(countResult, operation);
        std::vector<Property> properties(propertyCount);
        if (propertyCount == 0) {
            return properties;
        }
        const auto fillResult = enumerate(&propertyCount, properties.data());
        if (fillResult == VK_INCOMPLETE) {
            continue;
        }
        checkResult(fillResult, operation);
        properties.resize(propertyCount);
        return properties;
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL reportVulkanMessage(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT* message, void* userContext) noexcept {
    // C stdio and a stateless callback keep allocation/exception state out of the C API boundary.
    (void)messageTypes;
    (void)userContext;
    std::fprintf(stderr, "[Vulkan %s] [%s] %s\n",
                 (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "ERROR" : "WARNING",
                 message && message->pMessageIdName ? message->pMessageIdName : "unknown",
                 message && message->pMessage ? message->pMessage : "(no message text)");
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT makeDebugMessengerInfo() {
    VkDebugUtilsMessengerCreateInfoEXT messengerInfo{};
    messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messengerInfo.pfnUserCallback = reportVulkanMessage;
    return messengerInfo;
}
} // namespace

VulkanContext::~VulkanContext() noexcept {
    cleanup();
}

void VulkanContext::initialize(GLFWwindow* windowHandle) {
    if (!windowHandle || instanceHandle != VK_NULL_HANDLE) {
        throw std::runtime_error("Vulkan initialization requires a window and an empty context.");
    }
    createInstance();
#ifdef ZL_RENDERER_TESTING
    runVulkanTestCheckpoint("instance", instanceHandle);
#endif
    createDebugMessenger();
#ifdef ZL_RENDERER_TESTING
    runVulkanTestCheckpoint("messenger", instanceHandle);
#endif
    VkSurfaceKHR createdSurfaceHandle = VK_NULL_HANDLE;
    checkResult(glfwCreateWindowSurface(instanceHandle, windowHandle, nullptr, &createdSurfaceHandle),
                "surface: glfwCreateWindowSurface");
    surfaceHandle = createdSurfaceHandle;
    std::puts("Created surface");
#ifdef ZL_RENDERER_TESTING
    runVulkanTestCheckpoint("surface", instanceHandle);
#endif
    selectPhysicalDevice();
    createLogicalDevice();
#ifdef ZL_RENDERER_TESTING
    runVulkanTestCheckpoint("device", instanceHandle);
#endif
    vkGetDeviceQueue(deviceHandle, queueFamilies.graphicsFamilyIndex, 0, &graphicsQueueHandle);
    vkGetDeviceQueue(deviceHandle, queueFamilies.presentFamilyIndex, 0, &presentQueueHandle);
    if (!graphicsQueueHandle || !presentQueueHandle) {
        throw std::runtime_error("queues: vkGetDeviceQueue returned a null queue.");
    }
    std::printf("Queues: graphics=%p present=%p (index=0)\n",
                static_cast<void*>(graphicsQueueHandle), static_cast<void*>(presentQueueHandle));
    std::puts("Vulkan initialization succeeded");
#ifdef ZL_RENDERER_TESTING
    // The diagnostic test exits through the application's unchanged event-loop condition.
    if (isVulkanTestCase("diagnostic")) {
        glfwSetWindowShouldClose(windowHandle, GLFW_TRUE);
    }
#endif
}

void VulkanContext::createInstance() {
    if (glfwVulkanSupported() != GLFW_TRUE) {
        throw std::runtime_error("instance prerequisites: GLFW reports no Vulkan loader/ICD support.");
    }
    const auto layers = enumerateProperties<VkLayerProperties>(
        [](auto* count, auto* properties) { return vkEnumerateInstanceLayerProperties(count, properties); },
        "instance prerequisites: vkEnumerateInstanceLayerProperties");
    auto hasValidationLayer = std::ranges::any_of(layers, [](const auto& layer) {
        return std::strcmp(layer.layerName, validationLayerName) == 0;
    });
#ifdef ZL_RENDERER_TESTING
    if (isVulkanTestCase("missing-layer")) {
        hasValidationLayer = false;
    }
#endif
    if (!hasValidationLayer) {
        throw std::runtime_error(std::string("instance prerequisites: missing layer ") + validationLayerName);
    }
    std::uint32_t requiredExtensionCount = 0;
    const auto requiredExtensions = glfwGetRequiredInstanceExtensions(&requiredExtensionCount);
    if (!requiredExtensions || requiredExtensionCount == 0) {
        throw std::runtime_error("instance prerequisites: GLFW required surface extensions unavailable.");
    }
    std::vector<const char*> extensionNames(requiredExtensions, requiredExtensions + requiredExtensionCount);
    extensionNames.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    std::ranges::sort(extensionNames, [](const char* left, const char* right) {
        return std::strcmp(left, right) < 0;
    });
    const auto duplicates = std::ranges::unique(extensionNames, [](const char* left, const char* right) {
        return std::strcmp(left, right) == 0;
    });
    extensionNames.erase(duplicates.begin(), duplicates.end());
    const auto extensions = enumerateProperties<VkExtensionProperties>(
        [](auto* count, auto* properties) { return vkEnumerateInstanceExtensionProperties(nullptr, count, properties); },
        "instance prerequisites: vkEnumerateInstanceExtensionProperties");
    for (const auto extensionName : extensionNames) {
        auto hasExtension = std::ranges::any_of(extensions, [extensionName](const auto& extension) {
            return std::strcmp(extension.extensionName, extensionName) == 0;
        });
#ifdef ZL_RENDERER_TESTING
        if (isVulkanTestCase("missing-extension") &&
            std::strcmp(extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) {
            hasExtension = false;
        }
#endif
        if (!hasExtension) {
            throw std::runtime_error(std::string("instance prerequisites: missing extension ") + extensionName);
        }
    }

    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "ZLRenderer";
    applicationInfo.apiVersion = VK_API_VERSION_1_0;
    const auto messengerInfo = makeDebugMessengerInfo();
    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pNext = &messengerInfo;
    instanceInfo.pApplicationInfo = &applicationInfo;
    instanceInfo.enabledLayerCount = 1;
    instanceInfo.ppEnabledLayerNames = &validationLayerName;
    instanceInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensionNames.size());
    instanceInfo.ppEnabledExtensionNames = extensionNames.data();
    VkInstance createdInstanceHandle = VK_NULL_HANDLE;
    checkResult(vkCreateInstance(&instanceInfo, nullptr, &createdInstanceHandle), "instance: vkCreateInstance");
    instanceHandle = createdInstanceHandle;
    std::puts("Created instance; validation enabled: VK_LAYER_KHRONOS_validation (API 1.0)");
}

void VulkanContext::createDebugMessenger() {
    const auto createDebugMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instanceHandle, "vkCreateDebugUtilsMessengerEXT"));
    destroyDebugMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instanceHandle, "vkDestroyDebugUtilsMessengerEXT"));
    if (!createDebugMessenger || !destroyDebugMessenger) {
        throw std::runtime_error("messenger: debug-utils create/destroy entry points unavailable.");
    }
    const auto messengerInfo = makeDebugMessengerInfo();
    VkDebugUtilsMessengerEXT createdMessengerHandle = VK_NULL_HANDLE;
    checkResult(createDebugMessenger(instanceHandle, &messengerInfo, nullptr, &createdMessengerHandle),
                "messenger: vkCreateDebugUtilsMessengerEXT");
    debugMessengerHandle = createdMessengerHandle;
    std::puts("Created debug messenger");
}

void VulkanContext::selectPhysicalDevice() {
    const auto physicalDevices = enumerateProperties<VkPhysicalDevice>(
        [this](auto* count, auto* devices) { return vkEnumeratePhysicalDevices(instanceHandle, count, devices); },
        "physical device selection: vkEnumeratePhysicalDevices");
    for (const auto candidateHandle : physicalDevices) {
        std::uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidateHandle, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> familyProperties(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidateHandle, &familyCount, familyProperties.data());
        familyProperties.resize(familyCount);
        std::vector<QueueFamilySupport> families;
        families.reserve(familyCount);
        for (std::uint32_t familyIndex = 0; familyIndex < familyCount; ++familyIndex) {
            const auto& properties = familyProperties[familyIndex];
            VkBool32 supportsPresent = VK_FALSE;
            if (properties.queueCount != 0) {
                checkResult(vkGetPhysicalDeviceSurfaceSupportKHR(candidateHandle, familyIndex, surfaceHandle,
                                                                &supportsPresent),
                            "physical device selection: vkGetPhysicalDeviceSurfaceSupportKHR");
            }
            families.push_back({properties.queueCount,
                                (properties.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0,
                                supportsPresent == VK_TRUE});
        }
        auto selection = selectQueueFamilies(families);
#ifdef ZL_RENDERER_TESTING
        if (isVulkanTestCase("no-device")) {
            selection.reset();
        }
#endif
        if (selection) {
            physicalDeviceHandle = candidateHandle;
            queueFamilies = *selection;
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(physicalDeviceHandle, &properties);
            std::printf("Selected device: %s; graphicsFamily=%u presentFamily=%u\n", properties.deviceName,
                        queueFamilies.graphicsFamilyIndex, queueFamilies.presentFamilyIndex);
            return;
        }
    }
    throw std::runtime_error("physical device selection: no suitable graphics/present device for this surface.");
}

void VulkanContext::createLogicalDevice() {
    const auto uniqueFamilies = getUniqueQueueFamilies(queueFamilies);
    constexpr float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    queueInfos.reserve(uniqueFamilies.size());
    for (const auto familyIndex : uniqueFamilies) {
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = familyIndex;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;
        queueInfos.push_back(queueInfo);
    }
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queueInfos.size());
    deviceInfo.pQueueCreateInfos = queueInfos.data();
    VkDevice createdDeviceHandle = VK_NULL_HANDLE;
    VkResult createResult;
#ifdef ZL_RENDERER_TESTING
    if (isVulkanTestCase("create-device")) {
        createResult = VK_ERROR_INITIALIZATION_FAILED;
    } else
#endif
    {
        createResult = vkCreateDevice(physicalDeviceHandle, &deviceInfo, nullptr, &createdDeviceHandle);
    }
    checkResult(createResult, "logical device: vkCreateDevice");
    deviceHandle = createdDeviceHandle;
    std::printf("Created device; requested queue families=%u\n", deviceInfo.queueCreateInfoCount);
}

void VulkanContext::cleanup() noexcept {
    if (deviceHandle != VK_NULL_HANDLE) {
        // No GPU work is submitted in this change; future submissions require a completion contract.
        vkDestroyDevice(deviceHandle, nullptr);
        deviceHandle = VK_NULL_HANDLE;
        graphicsQueueHandle = VK_NULL_HANDLE;
        presentQueueHandle = VK_NULL_HANDLE;
        std::puts("Destroyed device");
    }
    if (surfaceHandle != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instanceHandle, surfaceHandle, nullptr);
        surfaceHandle = VK_NULL_HANDLE;
        std::puts("Destroyed surface");
    }
    if (debugMessengerHandle != VK_NULL_HANDLE) {
        destroyDebugMessenger(instanceHandle, debugMessengerHandle, nullptr);
        debugMessengerHandle = VK_NULL_HANDLE;
        std::puts("Destroyed debug messenger");
    }
    if (instanceHandle != VK_NULL_HANDLE) {
        vkDestroyInstance(instanceHandle, nullptr);
        instanceHandle = VK_NULL_HANDLE;
        physicalDeviceHandle = VK_NULL_HANDLE;
        std::puts("Destroyed instance");
    }
}
