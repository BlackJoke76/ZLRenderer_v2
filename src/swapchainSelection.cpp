#include "swapchainSelection.h"
#include "vulkanUtilities.h"
#include <algorithm>
#include <array>
#include <limits>

std::vector<VkSurfaceFormatKHR> expandSurfaceFormats(std::span<const VkSurfaceFormatKHR> formats) {
    if (formats.size() == 1 && formats.front().format == VK_FORMAT_UNDEFINED) {
        std::vector<VkSurfaceFormatKHR> candidates;
        for (const auto format : {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB,
                                  VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM}) {
            candidates.push_back({format, formats.front().colorSpace});
        }
        return candidates;
    }
    return {formats.begin(), formats.end()};
}

SurfaceSupport querySurfaceSupport(VkPhysicalDevice physicalDeviceHandle, VkSurfaceKHR surfaceHandle) {
    SurfaceSupport support;
    checkResult(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDeviceHandle, surfaceHandle, &support.capabilities),
                "surface: vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    const auto reportedFormats = enumerateProperties<VkSurfaceFormatKHR>(
        [=](auto* count, auto* formats) {
            return vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDeviceHandle, surfaceHandle, count, formats);
        }, "surface: vkGetPhysicalDeviceSurfaceFormatsKHR");
    for (const auto& surfaceFormat : expandSurfaceFormats(reportedFormats)) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDeviceHandle, surfaceFormat.format, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0) {
            support.formats.push_back(surfaceFormat);
        }
    }
    support.presentModes = enumerateProperties<VkPresentModeKHR>(
        [=](auto* count, auto* modes) {
            return vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDeviceHandle, surfaceHandle, count, modes);
        }, "surface: vkGetPhysicalDeviceSurfacePresentModesKHR");
    return support;
}

std::optional<VkPresentModeKHR> selectPresentMode(std::span<const VkPresentModeKHR> modes) {
    for (const auto preferred : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR}) {
        if (std::ranges::find(modes, preferred) != modes.end()) { return preferred; }
    }
    return std::nullopt;
}

std::optional<VkSurfaceFormatKHR> selectSurfaceFormat(std::span<const VkSurfaceFormatKHR> formats) {
    if (formats.empty()) { return std::nullopt; }
    const auto preferred = std::ranges::find_if(formats, [](const auto& format) {
        return format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    return preferred != formats.end() ? *preferred : formats.front();
}

bool supportsClearPresentation(const SurfaceSupport& support) {
    return (support.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0 &&
           selectSurfaceFormat(support.formats).has_value() && selectPresentMode(support.presentModes).has_value();
}

SwapchainConfiguration selectSwapchainConfiguration(const SurfaceSupport& support, VkExtent2D framebufferExtent,
                                                    const QueueFamilySelection& families) {
    if (!supportsClearPresentation(support)) { throw std::runtime_error("swapchain: no clear presentation configuration."); }
    if (framebufferExtent.width == 0 || framebufferExtent.height == 0) {
        throw std::runtime_error("swapchain: zero framebuffer extent must be deferred.");
    }
    SwapchainConfiguration configuration;
    configuration.surfaceFormat = *selectSurfaceFormat(support.formats);
    configuration.presentMode = *selectPresentMode(support.presentModes);
    const auto& capabilities = support.capabilities;
    configuration.extent = capabilities.currentExtent;
    if (capabilities.currentExtent.width == std::numeric_limits<std::uint32_t>::max()) {
        configuration.extent = {std::clamp(framebufferExtent.width, capabilities.minImageExtent.width,
                                          capabilities.maxImageExtent.width),
                                std::clamp(framebufferExtent.height, capabilities.minImageExtent.height,
                                          capabilities.maxImageExtent.height)};
    }
    configuration.imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount != 0) {
        configuration.imageCount = std::min(configuration.imageCount, capabilities.maxImageCount);
    }
    for (const auto alpha : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                             VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
        if ((capabilities.supportedCompositeAlpha & alpha) != 0) {
            configuration.compositeAlpha = alpha;
            break;
        }
    }
    if (configuration.compositeAlpha == 0) { throw std::runtime_error("swapchain: no supported composite alpha."); }
    configuration.transform = capabilities.currentTransform;
    configuration.familyIndices = getUniqueQueueFamilies(families);
    configuration.sharingMode = configuration.familyIndices.size() == 1 ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
    return configuration;
}
