#pragma once

#include <filesystem>
#include <vector>

namespace Os {
std::filesystem::path GetExecutableDirectory();
std::vector<char> ReadContentFromBinaryFile(char const* filename);
std::vector<char> LoadTextureData(char const* filename, uint32_t& width, uint32_t& height);
} // namespace Os
