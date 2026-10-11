#pragma once

#include "queueSelection.h"
#include <vulkan/vulkan.h>

struct GLFWwindow;

class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext() noexcept;
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;

    void initialize(GLFWwindow* windowHandle);

private:
    void createInstance();
    void createDebugMessenger();
    void selectPhysicalDevice();
    void createLogicalDevice();
    void cleanup() noexcept;

    VkInstance instanceHandle = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessengerHandle = VK_NULL_HANDLE;
    PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugMessenger = nullptr;
    VkSurfaceKHR surfaceHandle = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDeviceHandle = VK_NULL_HANDLE; // Borrowed from instance.
    VkDevice deviceHandle = VK_NULL_HANDLE;
    VkQueue graphicsQueueHandle = VK_NULL_HANDLE; // Borrowed from device.
    VkQueue presentQueueHandle = VK_NULL_HANDLE;
    QueueFamilySelection queueFamilies{};
};
