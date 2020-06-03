#include "Common.h"

#include <cstring>
#include <filesystem>
#include <fstream>

#define STB_IMAGE_IMPLEMENTATION
#include "core/stb_image.h"

namespace Os {
std::vector<char> ReadContentFromBinaryFile(char const* filename)
{
  std::vector<char> fileContent;
  std::filesystem::path filePath = Os::GetExecutableDirectory() / filename;
  std::ifstream file(filePath, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    throw std::runtime_error(std::string("Could not open shader file: ") + filePath.string());
  }

  size_t contentSize = file.tellg();
  fileContent.resize(contentSize);
  file.seekg(0);
  file.read(fileContent.data(), contentSize);
  file.close();
  return fileContent;
}

std::vector<char> LoadTextureData(char const* filename, uint32_t& width, uint32_t& height)
{
  int tempWidth, tempHeight;
  int components;
  std::vector<char> result = Os::ReadContentFromBinaryFile(filename);
  stbi_uc* imageData = stbi_load_from_memory(reinterpret_cast<stbi_uc const*>(result.data()),
                                             static_cast<int>(result.size()),
                                             &tempWidth,
                                             &tempHeight,
                                             &components,
                                             4);
  if (imageData == nullptr) {
    throw std::runtime_error("Could not load image");
  }

  width = tempWidth;
  height = tempHeight;
  uint32_t resultSize = width * height * /* number of components */ 4;
  result.resize(resultSize, '\0');
  memcpy(result.data(), imageData, resultSize);
  stbi_image_free(imageData);
  return result;
}
} // namespace Os
