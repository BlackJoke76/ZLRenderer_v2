#include "shaderFile.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <fstream>
#include <stdexcept>

std::filesystem::path getShaderDirectory() {
    std::vector<wchar_t> executablePath(32768);
    const auto length = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
    if (length == 0 || length >= executablePath.size()) { throw std::runtime_error("shader path: cannot locate executable"); }
    return std::filesystem::path(std::wstring(executablePath.data(), length)).parent_path() / "shaders";
}
std::vector<std::uint32_t> readShaderFile(const std::filesystem::path& shaderPath) {
    std::ifstream shaderFile(shaderPath, std::ios::binary | std::ios::ate);
    const auto fail = [&shaderPath](const char* reason) {
        return std::runtime_error("shader load: " + shaderPath.string() + ": " + reason);
    };
    if (!shaderFile) { throw fail("cannot open file"); }
    const auto byteCount = shaderFile.tellg();
    if (byteCount < 20 || byteCount % 4 != 0) { throw fail("invalid SPIR-V byte count"); }
    std::vector<std::uint32_t> words(static_cast<std::size_t>(byteCount) / sizeof(std::uint32_t));
    shaderFile.seekg(0);
    if (!shaderFile.read(reinterpret_cast<char*>(words.data()), byteCount)) { throw fail("cannot read file"); }
    if (words[0] != 0x07230203 || words[1] != 0x00010000 || words[3] == 0 || words[4] != 0) {
        throw fail("invalid SPIR-V 1.0 header");
    }
    return words;
}
