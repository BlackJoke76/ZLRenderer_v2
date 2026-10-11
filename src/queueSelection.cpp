#include "queueSelection.h"

std::optional<QueueFamilySelection> selectQueueFamilies(std::span<const QueueFamilySupport> families) {
    std::optional<std::uint32_t> graphicsFamilyIndex;
    std::optional<std::uint32_t> presentFamilyIndex;
    for (std::uint32_t familyIndex = 0; familyIndex < families.size(); ++familyIndex) {
        const auto& family = families[familyIndex];
        if (family.queueCount == 0) {
            continue;
        }
        if (family.supportsGraphics && family.supportsPresent) {
            return QueueFamilySelection{familyIndex, familyIndex};
        }
        if (family.supportsGraphics && !graphicsFamilyIndex) {
            graphicsFamilyIndex = familyIndex;
        }
        if (family.supportsPresent && !presentFamilyIndex) {
            presentFamilyIndex = familyIndex;
        }
    }
    if (graphicsFamilyIndex && presentFamilyIndex) {
        return QueueFamilySelection{*graphicsFamilyIndex, *presentFamilyIndex};
    }
    return std::nullopt;
}

std::vector<std::uint32_t> getUniqueQueueFamilies(const QueueFamilySelection& selection) {
    std::vector<std::uint32_t> familyIndices{selection.graphicsFamilyIndex};
    if (selection.presentFamilyIndex != selection.graphicsFamilyIndex) {
        familyIndices.push_back(selection.presentFamilyIndex);
    }
    return familyIndices;
}
