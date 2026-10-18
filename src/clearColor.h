#pragma once
#include <array>
#include <cmath>
#include <stdexcept>

struct ClearColor {
    float red = 0.05f;
    float green = 0.10f;
    float blue = 0.20f;
    float alpha = 1.0f;
};

inline void validateClearColor(ClearColor clearColor) {
    for (const auto component : std::array{clearColor.red, clearColor.green, clearColor.blue, clearColor.alpha}) {
        if (!std::isfinite(component) || component < 0.0f || component > 1.0f) {
            throw std::runtime_error("RHI clear color must contain finite components in [0,1].");
        }
    }
}
