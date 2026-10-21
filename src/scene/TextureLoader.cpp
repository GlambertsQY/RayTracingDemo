#include "scene/TextureLoader.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstdio>

bool loadTextureFile(const std::string& path, TextureData& out, bool srgb)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels)
    {
        std::printf("Failed to load texture: %s\n", path.c_str());
        return false;
    }

    out.width = width;
    out.height = height;
    out.srgb = srgb;
    out.pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    stbi_image_free(pixels);
    return true;
}

bool loadTextureMemory(const unsigned char* data, int size, TextureData& out, bool srgb)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(data, size, &width, &height, &channels, 4);
    if (!pixels)
    {
        return false;
    }

    out.width = width;
    out.height = height;
    out.srgb = srgb;
    out.pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    stbi_image_free(pixels);
    return true;
}

bool savePng(const std::string& path, int width, int height, const unsigned char* rgba)
{
    return stbi_write_png(path.c_str(), width, height, 4, rgba, width * 4) != 0;
}
