#include "common/Assets.h"

#include "common/Log.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

#if defined(__ANDROID__)
struct EmbeddedShader
{
    const char*           name;
    const unsigned char*  data;
    unsigned int          size;
};

extern const EmbeddedShader g_embeddedShaders[];
extern const unsigned int   g_embeddedShaderCount;
#endif

namespace assets
{
namespace
{
#if defined(__ANDROID__)
AAssetManager* g_assetManager = nullptr;
#elif defined(_WIN32)
std::string g_assetRoot = RTDEMO_ASSET_DIR;

std::string executableDirectory()
{
    char buffer[MAX_PATH]{};
    GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    std::string path(buffer);
    const size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}
#endif
} // namespace

void setAssetManager(AAssetManager* manager)
{
#if defined(__ANDROID__)
    g_assetManager = manager;
#else
    (void)manager;
#endif
}

bool readFile(const std::string& path, std::vector<uint8_t>& out)
{
#if defined(__ANDROID__)
    if (!g_assetManager || path.empty())
    {
        return false;
    }

    AAsset* asset = AAssetManager_open(g_assetManager, path.c_str(), AASSET_MODE_BUFFER);
    if (!asset)
    {
        return false;
    }

    const off_t length = AAsset_getLength(asset);
    if (length <= 0)
    {
        AAsset_close(asset);
        return false;
    }

    out.resize(static_cast<size_t>(length));
    size_t offset = 0;
    while (offset < out.size())
    {
        const int read = AAsset_read(asset, out.data() + offset, out.size() - offset);
        if (read <= 0)
        {
            break;
        }
        offset += static_cast<size_t>(read);
    }
    AAsset_close(asset);

    if (offset != out.size())
    {
        out.clear();
        return false;
    }
    return true;
#else
    const std::string fullPath = g_assetRoot + "/" + path;
    std::ifstream file(fullPath, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        return false;
    }

    const std::streamsize length = file.tellg();
    if (length <= 0)
    {
        return false;
    }

    out.resize(static_cast<size_t>(length));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(out.data()), length);
    return file.good();
#endif
}

bool exists(const std::string& path)
{
#if defined(__ANDROID__)
    if (!g_assetManager || path.empty())
    {
        return false;
    }
    AAsset* asset = AAssetManager_open(g_assetManager, path.c_str(), AASSET_MODE_BUFFER);
    if (!asset)
    {
        return false;
    }
    AAsset_close(asset);
    return true;
#else
    return std::filesystem::exists(g_assetRoot + "/" + path);
#endif
}

size_t size(const std::string& path)
{
#if defined(__ANDROID__)
    if (!g_assetManager || path.empty())
    {
        return 0;
    }
    AAsset* asset = AAssetManager_open(g_assetManager, path.c_str(), AASSET_MODE_BUFFER);
    if (!asset)
    {
        return 0;
    }
    const off_t length = AAsset_getLength(asset);
    AAsset_close(asset);
    return length > 0 ? static_cast<size_t>(length) : 0;
#else
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(g_assetRoot + "/" + path, ec);
    return ec ? 0 : static_cast<size_t>(fileSize);
#endif
}

bool readShader(const char* name, std::vector<uint8_t>& out)
{
    if (!name || !*name)
    {
        return false;
    }

#if defined(__ANDROID__)
    for (unsigned int i = 0; i < g_embeddedShaderCount; ++i)
    {
        const EmbeddedShader& shader = g_embeddedShaders[i];
        if (shader.name && std::strcmp(shader.name, name) == 0)
        {
            out.assign(shader.data, shader.data + shader.size);
            return true;
        }
    }
    RT_LOG("Embedded shader not found: %s", name);
    return false;
#else
    const std::string path = executableDirectory() + "/shaders/" + name;
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        RT_LOG("Failed to open shader: %s", path.c_str());
        return false;
    }

    const std::streamsize length = file.tellg();
    if (length <= 0)
    {
        return false;
    }
    out.resize(static_cast<size_t>(length));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(out.data()), length);
    return file.good();
#endif
}
} // namespace assets
