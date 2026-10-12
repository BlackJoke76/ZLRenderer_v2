#pragma once

#include "queueSelection.h"
#include <vulkan/vulkan.h>
#include <optional>
#include <span>
#include <vector>

struct SurfaceSupport {
    VkSurfaceCapabilitiesKHR capabilities{};
    // Only formats supporting optimal-tiling color attachments are retained.
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

struct SwapchainConfiguration {
    VkSurfaceFormatKHR surfaceFormat{};
    VkPresentModeKHR presentMode{};
    VkExtent2D extent{};
    std::uint32_t imageCount{};
    VkCompositeAlphaFlagBitsKHR compositeAlpha{};
    VkSurfaceTransformFlagBitsKHR transform{};
    VkSharingMode sharingMode{};
    std::vector<std::uint32_t> familyIndices;
};

std::vector<VkSurfaceFormatKHR> expandSurfaceFormats(std::span<const VkSurfaceFormatKHR> formats);
SurfaceSupport querySurfaceSupport(VkPhysicalDevice physicalDeviceHandle, VkSurfaceKHR surfaceHandle);
std::optional<VkPresentModeKHR> selectPresentMode(std::span<const VkPresentModeKHR> modes);
std::optional<VkSurfaceFormatKHR> selectSurfaceFormat(std::span<const VkSurfaceFormatKHR> formats);
bool supportsClearPresentation(const SurfaceSupport& support);
SwapchainConfiguration selectSwapchainConfiguration(const SurfaceSupport& support, VkExtent2D framebufferExtent,
                                                    const QueueFamilySelection& families);
