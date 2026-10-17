#include "shaderFile.h"
#include <array>
#include <fstream>
#include <cstdio>
#include <stdexcept>

int main() {
    const auto fixturePath = getShaderDirectory().parent_path() / "shader-file-test.spv";
    const auto expectFailure = [&] {
        try { (void)readShaderFile(fixturePath); }
        catch (const std::runtime_error&) { return; }
        throw std::runtime_error("Invalid shader unexpectedly accepted");
    };
    try {
        std::filesystem::remove(fixturePath);
        expectFailure();
        for (const auto byteCount : {0, 3, 20, 24}) {
            { std::ofstream fixture(fixturePath, std::ios::binary); for (int index = 0; index < byteCount; ++index) { fixture.put(0); } }
            expectFailure();
        }
        const std::array<std::uint32_t, 5> header{0x07230203, 0x00010000, 0, 1, 0};
        { std::ofstream fixture(fixturePath, std::ios::binary); fixture.write(reinterpret_cast<const char*>(header.data()), sizeof(header)); }
        if (readShaderFile(fixturePath).size() != header.size()) { throw std::runtime_error("Header read mismatch"); }
        std::filesystem::remove(fixturePath);
        std::puts("PASS missing/empty/misaligned/corrupt header and valid header loading");
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove(fixturePath);
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
