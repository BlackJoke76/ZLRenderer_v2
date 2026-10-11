#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

struct QueueFamilySupport {
    std::uint32_t queueCount;
    bool supportsGraphics;
    bool supportsPresent;
};

struct QueueFamilySelection {
    std::uint32_t graphicsFamilyIndex;
    std::uint32_t presentFamilyIndex;
};

std::optional<QueueFamilySelection> selectQueueFamilies(std::span<const QueueFamilySupport> families);
std::vector<std::uint32_t> getUniqueQueueFamilies(const QueueFamilySelection& selection);
