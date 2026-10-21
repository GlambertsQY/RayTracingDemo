#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct AAssetManager;

namespace assets
{
void setAssetManager(AAssetManager* manager);

bool   readFile(const std::string& path, std::vector<uint8_t>& out);
bool   exists(const std::string& path);
size_t size(const std::string& path);
bool   readShader(const char* name, std::vector<uint8_t>& out);
} // namespace assets
