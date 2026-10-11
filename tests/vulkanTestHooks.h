#pragma once
#include <vulkan/vulkan.h>

bool isVulkanTestCase(const char* caseName);
void runVulkanTestCheckpoint(const char* stage, VkInstance instanceHandle);
