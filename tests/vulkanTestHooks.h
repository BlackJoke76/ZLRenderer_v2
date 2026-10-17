#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>

bool isVulkanTestCase(const char* caseName);
void runVulkanTestCheckpoint(const char* stage, VkInstance instanceHandle);
void runSwapchainTestCheckpoint(const char* stage, std::uint32_t generation);
bool shouldRequestSwapchainTestRebuild(std::uint64_t submittedFrames, std::uint32_t generation);
struct GLFWwindow;
void runPresentationPreparationTest(GLFWwindow* windowHandle, std::uint64_t submittedFrames);
void applyWindowStateTest(const char* stage, GLFWwindow* windowHandle, std::uint64_t submittedFrames,
    int& framebufferWidth, int& framebufferHeight, bool& iconified, bool& closing);
void applySurfaceExtentTest(VkSurfaceCapabilitiesKHR& capabilities, std::uint64_t submittedFrames);
