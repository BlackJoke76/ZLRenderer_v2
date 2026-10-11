#include "queueSelection.h"

#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* description) {
    if (!condition) {
        throw std::runtime_error(description);
    }
}
} // namespace

int main() {
    try {
        const std::array unified{QueueFamilySupport{1, true, false}, QueueFamilySupport{1, false, true},
                                 QueueFamilySupport{1, true, true}};
        const auto sharedSelection = selectQueueFamilies(unified);
        require(sharedSelection && sharedSelection->graphicsFamilyIndex == 2 &&
                sharedSelection->presentFamilyIndex == 2, "Prefer a later shared family over an earlier split pair");
        require(getUniqueQueueFamilies(*sharedSelection) == std::vector<std::uint32_t>{2},
                "Request the shared family once");

        const std::array split{QueueFamilySupport{1, false, true}, QueueFamilySupport{1, true, false}};
        const auto splitSelection = selectQueueFamilies(split);
        require(splitSelection && splitSelection->graphicsFamilyIndex == 1 &&
                splitSelection->presentFamilyIndex == 0, "Accept separate families, including index zero");
        require(getUniqueQueueFamilies(*splitSelection) == std::vector<std::uint32_t>{1, 0},
                "Request both separate families exactly once");

        const std::array emptyShared{QueueFamilySupport{0, true, true}, QueueFamilySupport{1, true, false},
                                     QueueFamilySupport{1, false, true}};
        const auto nonemptySelection = selectQueueFamilies(emptyShared);
        require(nonemptySelection && nonemptySelection->graphicsFamilyIndex == 1 &&
                nonemptySelection->presentFamilyIndex == 2, "Ignore an empty shared family");
        const std::array missingGraphics{QueueFamilySupport{1, false, true}};
        const std::array missingPresent{QueueFamilySupport{1, true, false}};
        const std::array emptyOnly{QueueFamilySupport{0, true, true}};
        require(!selectQueueFamilies(missingGraphics), "Reject missing graphics support");
        require(!selectQueueFamilies(missingPresent), "Reject missing present support");
        require(!selectQueueFamilies(emptyOnly), "Reject empty queues");
        require(!selectQueueFamilies({}), "Reject an empty family list");
        std::cout << "PASS: shared preference, split pair, empty/missing capabilities, unique device families\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
