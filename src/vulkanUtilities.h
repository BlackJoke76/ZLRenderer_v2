#pragma once

#include <vulkan/vulkan.h>
#include <concepts>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

inline void checkResult(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed (VkResult=" +
                                 std::to_string(result) + ").");
    }
}

template <typename Property, typename Enumerate>
    requires std::invocable<Enumerate, std::uint32_t*, Property*>
std::vector<Property> enumerateProperties(Enumerate enumerate, const char* operation) {
    for (;;) {
        std::uint32_t propertyCount = 0;
        const auto countResult = enumerate(&propertyCount, static_cast<Property*>(nullptr));
        if (countResult == VK_INCOMPLETE) { continue; }
        checkResult(countResult, operation);
        std::vector<Property> properties(propertyCount);
        if (propertyCount == 0) { return properties; }
        const auto fillResult = enumerate(&propertyCount, properties.data());
        if (fillResult == VK_INCOMPLETE) { continue; }
        checkResult(fillResult, operation);
        properties.resize(propertyCount);
        return properties;
    }
}
