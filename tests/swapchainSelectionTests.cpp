#include "swapchainSelection.h"
#include "vulkanUtilities.h"
#include <array>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* reason) {
    if (!condition) { throw std::runtime_error(reason); }
}
SurfaceSupport makeSupport() {
    SurfaceSupport support;
    support.capabilities.minImageCount = 2;
    support.capabilities.currentExtent = {UINT32_MAX, UINT32_MAX};
    support.capabilities.minImageExtent = {64, 64};
    support.capabilities.maxImageExtent = {1920, 1080};
    support.capabilities.supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    support.capabilities.supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    support.capabilities.currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    support.formats = {{VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
                       {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}};
    support.presentModes = {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR};
    return support;
}
}

int main() {
    try {
        auto support = makeSupport();
        auto configuration = selectSwapchainConfiguration(support, {1280, 720}, {0, 0});
        require(configuration.presentMode == VK_PRESENT_MODE_MAILBOX_KHR, "MAILBOX preference");
        require(configuration.surfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB, "sRGB preference");
        require(configuration.imageCount == 3, "zero max is unbounded");
        require(configuration.sharingMode == VK_SHARING_MODE_EXCLUSIVE && configuration.familyIndices.size() == 1,
                "shared family");
        support.presentModes = {VK_PRESENT_MODE_FIFO_KHR};
        support.capabilities.maxImageCount = 2;
        support.capabilities.supportedCompositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        configuration = selectSwapchainConfiguration(support, {1, 9999}, {2, 4});
        require(configuration.presentMode == VK_PRESENT_MODE_FIFO_KHR, "FIFO fallback");
        require(configuration.imageCount == 2, "bounded image count");
        require(configuration.extent.width == 64 && configuration.extent.height == 1080, "extent clamp");
        require(configuration.compositeAlpha == VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, "alpha fallback");
        require(configuration.sharingMode == VK_SHARING_MODE_CONCURRENT &&
                configuration.familyIndices == std::vector<std::uint32_t>{2, 4}, "separate families");
        support.capabilities.currentExtent = {800, 600};
        support.capabilities.currentTransform = VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
        configuration = selectSwapchainConfiguration(support, {1280, 720}, {0, 0});
        require(configuration.extent.width == 800 && configuration.extent.height == 600, "fixed extent");
        require(configuration.transform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR, "current transform");
        const std::array undefinedFormats{VkSurfaceFormatKHR{VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}};
        require(expandSurfaceFormats(undefinedFormats).front().format == VK_FORMAT_B8G8R8A8_SRGB, "undefined format expansion");
        support.formats.resize(1);
        require(selectSurfaceFormat(support.formats)->format == VK_FORMAT_R8G8B8A8_UNORM, "format fallback");
        support.formats.clear();
        require(!supportsClearPresentation(support), "empty format rejection");
        support = makeSupport();
        support.presentModes = {VK_PRESENT_MODE_IMMEDIATE_KHR};
        require(!supportsClearPresentation(support), "no supported mode rejection");
        support = makeSupport();
        support.capabilities.supportedUsageFlags = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        require(!supportsClearPresentation(support), "missing color usage rejection");
        support = makeSupport();
        bool rejectedZero = false;
        try { (void)selectSwapchainConfiguration(support, {0, 0}, {0, 0}); }
        catch (const std::runtime_error&) { rejectedZero = true; }
        require(rejectedZero, "zero extent deferred");
        for (const auto zeroExtent : {VkExtent2D{0, 0}, VkExtent2D{0, 480}, VkExtent2D{640, 0}}) {
            support.capabilities.currentExtent = zeroExtent;
            const auto selectedExtent = selectSwapchainExtent(support.capabilities, {640, 480});
            require(selectedExtent.width == 0 || selectedExtent.height == 0, "fixed zero extent deferred");
        }
        support = makeSupport();
        support.capabilities.minImageExtent = {0, 0};
        support.capabilities.maxImageExtent = {0, 0};
        require(selectSwapchainExtent(support.capabilities, {640, 480}).width == 0, "variable zero extent deferred");
        support = makeSupport();
        support.formats = {{VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}};
        support.presentModes = {VK_PRESENT_MODE_FIFO_KHR};
        support.capabilities.minImageCount = 1;
        support.capabilities.maxImageCount = 2;
        configuration = selectSwapchainConfiguration(support, {640, 480}, {0, 0});
        require(configuration.surfaceFormat.format == VK_FORMAT_R8G8B8A8_UNORM && configuration.imageCount == 2 &&
                configuration.presentMode == VK_PRESENT_MODE_FIFO_KHR, "changed generation configuration");
        unsigned fillCalls = 0;
        const auto enumerated = enumerateProperties<unsigned>([&](auto* count, auto* values) {
            *count = 1;
            if (!values) { return VK_SUCCESS; }
            if (++fillCalls == 1) { return VK_INCOMPLETE; }
            *values = 42;
            return VK_SUCCESS;
        }, "test enumeration");
        require(fillCalls == 2 && enumerated == std::vector<unsigned>{42}, "enumeration retries incomplete");
        std::puts("PASS: swapchain policies, separate families, zero extent and incomplete enumeration");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
