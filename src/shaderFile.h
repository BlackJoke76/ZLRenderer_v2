#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>
std::filesystem::path getShaderDirectory();
std::vector<std::uint32_t> readShaderFile(const std::filesystem::path& shaderPath);
