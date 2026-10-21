#pragma once

#include "scene/Scene.h"
#include <string>

bool loadTextureFile(const std::string& path, TextureData& out, bool srgb);
bool loadTextureMemory(const unsigned char* data, int size, TextureData& out, bool srgb);
bool savePng(const std::string& path, int width, int height, const unsigned char* rgba);
