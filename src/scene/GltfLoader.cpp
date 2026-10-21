#include "scene/GltfLoader.h"

#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

#include "common/Assets.h"
#include "common/Log.h"
#include "scene/TextureLoader.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <functional>
#include <map>
#include <utility>

namespace
{
const unsigned char* accessorData(const tinygltf::Model& model, const tinygltf::Accessor& accessor, size_t& stride)
{
    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[view.buffer];
    stride = view.byteStride;
    if (stride == 0)
    {
        stride = tinygltf::GetComponentSizeInBytes(accessor.componentType) *
                 tinygltf::GetNumComponentsInType(accessor.type);
    }
    return buffer.data.data() + view.byteOffset + accessor.byteOffset;
}

bool skipImageLoader(tinygltf::Image*, const int, std::string*, std::string*, int, int, const unsigned char*, int,
                     void*)
{
    // 图像由 stb_image 在 TextureLoader 中自行解码，这里只需让 tinygltf 跳过解码
    return true;
}

void setupAssetCallbacks(tinygltf::TinyGLTF& loader)
{
    tinygltf::FsCallbacks callbacks{};
    callbacks.FileExists = [](const std::string& path, void*) { return assets::exists(path); };
    callbacks.ExpandFilePath = [](const std::string& path, void*) { return path; };
    callbacks.ReadWholeFile = [](std::vector<unsigned char>* out, std::string* err, const std::string& path,
                                 void*) {
        std::vector<uint8_t> data;
        if (!assets::readFile(path, data))
        {
            if (err)
            {
                *err += "Failed to read asset: " + path;
            }
            return false;
        }
        out->assign(data.begin(), data.end());
        return true;
    };
    callbacks.WriteWholeFile = [](std::string* err, const std::string&, const std::vector<unsigned char>&, void*) {
        if (err)
        {
            *err += "Asset writing is not supported";
        }
        return false;
    };
    callbacks.GetFileSizeInBytes = [](size_t* fileSize, std::string* err, const std::string& path, void*) {
        const size_t length = assets::size(path);
        if (length == 0)
        {
            if (err)
            {
                *err += "Failed to stat asset: " + path;
            }
            return false;
        }
        *fileSize = length;
        return true;
    };

    std::string error;
    if (!loader.SetFsCallbacks(callbacks, &error))
    {
        RT_LOG("tinygltf SetFsCallbacks failed: %s", error.c_str());
    }
}

std::string parentDirectory(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

glm::mat4 nodeTransform(const tinygltf::Node& node)
{
    if (node.matrix.size() == 16)
    {
        glm::mat4 m(1.0f);
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                m[column][row] = static_cast<float>(node.matrix[column * 4 + row]);
            }
        }
        return m;
    }

    glm::mat4 m(1.0f);
    if (node.translation.size() == 3)
    {
        m = glm::translate(m, glm::vec3(static_cast<float>(node.translation[0]),
                                        static_cast<float>(node.translation[1]),
                                        static_cast<float>(node.translation[2])));
    }
    if (node.rotation.size() == 4)
    {
        const glm::quat q(static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]),
                          static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2]));
        m *= glm::mat4_cast(q);
    }
    if (node.scale.size() == 3)
    {
        m = glm::scale(m, glm::vec3(static_cast<float>(node.scale[0]), static_cast<float>(node.scale[1]),
                                    static_cast<float>(node.scale[2])));
    }
    return m;
}
} // namespace

bool loadGltfScene(const std::string& filename, Scene& out, std::string& error)
{
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string warn;
    loader.SetImageLoader(skipImageLoader, nullptr);
    setupAssetCallbacks(loader);

    std::vector<uint8_t> fileData;
    if (!assets::readFile(filename, fileData))
    {
        error = "asset not found: " + filename;
        return false;
    }

    const std::string baseDir = parentDirectory(filename);
    const bool isBinary = filename.size() >= 4 && filename.compare(filename.size() - 4, 4, ".glb") == 0;
    const bool loaded =
        isBinary ? loader.LoadBinaryFromMemory(&model, &error, &warn, fileData.data(),
                                              static_cast<unsigned int>(fileData.size()), baseDir)
                 : loader.LoadASCIIFromString(&model, &error, &warn,
                                              reinterpret_cast<const char*>(fileData.data()),
                                              static_cast<unsigned int>(fileData.size()), baseDir);

    if (!warn.empty())
    {
        RT_LOG("glTF warning: %s", warn.c_str());
    }
    if (!loaded)
    {
        if (error.empty())
        {
            error = "unknown glTF loading error";
        }
        return false;
    }

    // ---------------------------------------------------------------- textures
    std::map<std::pair<int, bool>, int> textureCache;
    auto textureSlot = [&](int gltfTextureIndex, bool srgb) -> int {
        if (gltfTextureIndex < 0 || gltfTextureIndex >= static_cast<int>(model.textures.size()))
        {
            return -1;
        }
        const int imageIndex = model.textures[gltfTextureIndex].source;
        if (imageIndex < 0 || imageIndex >= static_cast<int>(model.images.size()))
        {
            return -1;
        }

        const auto key = std::make_pair(imageIndex, srgb);
        const auto it = textureCache.find(key);
        if (it != textureCache.end())
        {
            return it->second;
        }

        const tinygltf::Image& image = model.images[imageIndex];
        TextureData data;
        bool ok = false;
        if (!image.uri.empty() && image.uri.rfind("data:", 0) != 0)
        {
            const std::string imagePath = baseDir.empty() ? image.uri : baseDir + "/" + image.uri;
            std::vector<uint8_t> bytes;
            if (assets::readFile(imagePath, bytes))
            {
                ok = loadTextureMemory(bytes.data(), static_cast<int>(bytes.size()), data, srgb);
            }
        }
        else if (!image.image.empty())
        {
            ok = loadTextureMemory(image.image.data(), static_cast<int>(image.image.size()), data, srgb);
        }
        else if (image.bufferView >= 0)
        {
            const tinygltf::BufferView& view = model.bufferViews[image.bufferView];
            const tinygltf::Buffer& buffer = model.buffers[view.buffer];
            ok = loadTextureMemory(buffer.data.data() + view.byteOffset, static_cast<int>(view.byteLength), data,
                                   srgb);
        }

        if (!ok)
        {
            RT_LOG("Failed to decode glTF image %d (%s)", imageIndex, image.uri.c_str());
            return -1;
        }

        const int slot = static_cast<int>(out.textures.size());
        out.textures.push_back(std::move(data));
        textureCache[key] = slot;
        return slot;
    };

    // --------------------------------------------------------------- materials
    for (const tinygltf::Material& gltfMat : model.materials)
    {
        Material m;
        const tinygltf::PbrMetallicRoughness& pbr = gltfMat.pbrMetallicRoughness;
        if (pbr.baseColorFactor.size() >= 4)
        {
            m.baseColor = glm::vec3(static_cast<float>(pbr.baseColorFactor[0]),
                                    static_cast<float>(pbr.baseColorFactor[1]),
                                    static_cast<float>(pbr.baseColorFactor[2]));
        }
        else if (pbr.baseColorFactor.size() == 3)
        {
            m.baseColor = glm::vec3(static_cast<float>(pbr.baseColorFactor[0]),
                                    static_cast<float>(pbr.baseColorFactor[1]),
                                    static_cast<float>(pbr.baseColorFactor[2]));
        }

        m.metallic = static_cast<float>(pbr.metallicFactor);
        m.roughness = static_cast<float>(pbr.roughnessFactor);
        if (pbr.baseColorTexture.index >= 0)
        {
            m.baseColorTex = textureSlot(pbr.baseColorTexture.index, true);
        }
        if (pbr.metallicRoughnessTexture.index >= 0)
        {
            m.mrTex = textureSlot(pbr.metallicRoughnessTexture.index, false);
        }
        if (gltfMat.normalTexture.index >= 0)
        {
            m.normalTex = textureSlot(gltfMat.normalTexture.index, false);
        }
        if (gltfMat.emissiveFactor.size() == 3)
        {
            m.emissive = glm::vec3(static_cast<float>(gltfMat.emissiveFactor[0]),
                                   static_cast<float>(gltfMat.emissiveFactor[1]),
                                   static_cast<float>(gltfMat.emissiveFactor[2]));
        }

        if (gltfMat.alphaMode == "MASK")
        {
            m.alphaMode = 1;
            m.alphaCutoff = static_cast<float>(gltfMat.alphaCutoff);
        }
        else if (gltfMat.alphaMode == "BLEND")
        {
            m.alphaMode = 2;
        }
        out.materials.push_back(m);
    }

    if (out.materials.empty())
    {
        out.materials.push_back(Material{});
    }

    // ------------------------------------------------------------------ meshes
    auto addMesh = [&](const tinygltf::Mesh& gltfMesh, const glm::mat4& world) {
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(world)));

        for (const tinygltf::Primitive& prim : gltfMesh.primitives)
        {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES)
            {
                continue;
            }

            const auto posIt = prim.attributes.find("POSITION");
            if (posIt == prim.attributes.end())
            {
                continue;
            }

            const tinygltf::Accessor& posAccessor = model.accessors[posIt->second];
            size_t posStride = 0;
            const unsigned char* posData = accessorData(model, posAccessor, posStride);

            const tinygltf::Accessor* normalAccessor = nullptr;
            const unsigned char* normalData = nullptr;
            size_t normalStride = 0;
            const auto normalIt = prim.attributes.find("NORMAL");
            if (normalIt != prim.attributes.end())
            {
                normalAccessor = &model.accessors[normalIt->second];
                normalData = accessorData(model, *normalAccessor, normalStride);
            }

            const tinygltf::Accessor* uvAccessor = nullptr;
            const unsigned char* uvData = nullptr;
            size_t uvStride = 0;
            const auto uvIt = prim.attributes.find("TEXCOORD_0");
            if (uvIt != prim.attributes.end())
            {
                uvAccessor = &model.accessors[uvIt->second];
                uvData = accessorData(model, *uvAccessor, uvStride);
            }

            const tinygltf::Accessor* tangentAccessor = nullptr;
            const unsigned char* tangentData = nullptr;
            size_t tangentStride = 0;
            const auto tangentIt = prim.attributes.find("TANGENT");
            if (tangentIt != prim.attributes.end())
            {
                tangentAccessor = &model.accessors[tangentIt->second];
                tangentData = accessorData(model, *tangentAccessor, tangentStride);
            }

            Mesh mesh;
            mesh.material = prim.material >= 0 && prim.material < static_cast<int>(out.materials.size())
                                ? prim.material
                                : 0;
            mesh.vertices.resize(posAccessor.count);

            for (size_t i = 0; i < posAccessor.count; ++i)
            {
                Vertex& v = mesh.vertices[i];
                const float* p = reinterpret_cast<const float*>(posData + i * posStride);
                v.position = glm::vec3(world * glm::vec4(p[0], p[1], p[2], 1.0f));

                if (normalData)
                {
                    const float* n = reinterpret_cast<const float*>(normalData + i * normalStride);
                    v.normal = glm::normalize(normalMatrix * glm::vec3(n[0], n[1], n[2]));
                }
                else
                {
                    v.normal = glm::vec3(0.0f);
                }

                if (uvData)
                {
                    const float* uv = reinterpret_cast<const float*>(uvData + i * uvStride);
                    v.uv = glm::vec2(uv[0], uv[1]);
                }
                else
                {
                    v.uv = glm::vec2(0.0f);
                }

                if (tangentData)
                {
                    const float* t = reinterpret_cast<const float*>(tangentData + i * tangentStride);
                    v.tangent = glm::vec4(glm::normalize(glm::mat3(world) * glm::vec3(t[0], t[1], t[2])), t[3]);
                }
                else
                {
                    v.tangent = glm::vec4(0.0f);
                }
            }

            if (prim.indices >= 0)
            {
                const tinygltf::Accessor& indexAccessor = model.accessors[prim.indices];
                size_t indexStride = 0;
                const unsigned char* indexData = accessorData(model, indexAccessor, indexStride);
                mesh.indices.resize(indexAccessor.count);
                for (size_t i = 0; i < indexAccessor.count; ++i)
                {
                    if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
                    {
                        mesh.indices[i] = *reinterpret_cast<const uint16_t*>(indexData + i * indexStride);
                    }
                    else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
                    {
                        mesh.indices[i] = *reinterpret_cast<const uint8_t*>(indexData + i * indexStride);
                    }
                    else
                    {
                        mesh.indices[i] = *reinterpret_cast<const uint32_t*>(indexData + i * indexStride);
                    }
                }
            }
            else
            {
                mesh.indices.resize(posAccessor.count);
                for (size_t i = 0; i < posAccessor.count; ++i)
                {
                    mesh.indices[i] = static_cast<uint32_t>(i);
                }
            }

            // 缺失法线时按三角形面积加权生成
            if (!normalData)
            {
                for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
                {
                    Vertex& v0 = mesh.vertices[mesh.indices[i]];
                    Vertex& v1 = mesh.vertices[mesh.indices[i + 1]];
                    Vertex& v2 = mesh.vertices[mesh.indices[i + 2]];
                    const glm::vec3 n = glm::cross(v1.position - v0.position, v2.position - v0.position);
                    v0.normal += n;
                    v1.normal += n;
                    v2.normal += n;
                }
                for (auto& v : mesh.vertices)
                {
                    v.normal = glm::length(v.normal) > 1e-8f ? glm::normalize(v.normal) : glm::vec3(0, 1, 0);
                }
            }

            // 缺失切线时按 UV 生成
            if (!tangentData)
            {
                for (auto& v : mesh.vertices)
                {
                    v.tangent = glm::vec4(0.0f);
                }
                for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
                {
                    Vertex& v0 = mesh.vertices[mesh.indices[i]];
                    Vertex& v1 = mesh.vertices[mesh.indices[i + 1]];
                    Vertex& v2 = mesh.vertices[mesh.indices[i + 2]];
                    const glm::vec3 e1 = v1.position - v0.position;
                    const glm::vec3 e2 = v2.position - v0.position;
                    const glm::vec2 d1 = v1.uv - v0.uv;
                    const glm::vec2 d2 = v2.uv - v0.uv;
                    const float det = d1.x * d2.y - d2.x * d1.y;
                    if (std::abs(det) < 1e-8f)
                    {
                        continue;
                    }
                    const float invDet = 1.0f / det;
                    const glm::vec3 tangent = (e1 * d2.y - e2 * d1.y) * invDet;
                    v0.tangent += glm::vec4(tangent, 0.0f);
                    v1.tangent += glm::vec4(tangent, 0.0f);
                    v2.tangent += glm::vec4(tangent, 0.0f);
                }
                for (auto& v : mesh.vertices)
                {
                    glm::vec3 t(v.tangent);
                    t -= v.normal * glm::dot(v.normal, t);
                    v.tangent = glm::vec4(glm::length(t) > 1e-8f ? glm::normalize(t) : glm::vec3(1, 0, 0), 1.0f);
                }
            }

            const int meshIndex = static_cast<int>(out.meshes.size());
            out.meshes.push_back(std::move(mesh));
            out.instances.push_back(Instance{glm::mat4(1.0f), meshIndex, -1});
        }
    };

    std::function<void(int, const glm::mat4&)> visit = [&](int nodeIndex, const glm::mat4& parent) {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size()))
        {
            return;
        }
        const tinygltf::Node& node = model.nodes[nodeIndex];
        const glm::mat4 world = parent * nodeTransform(node);
        if (node.mesh >= 0 && node.mesh < static_cast<int>(model.meshes.size()))
        {
            addMesh(model.meshes[node.mesh], world);
        }
        for (int child : node.children)
        {
            visit(child, world);
        }
    };

    const int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (sceneIndex < static_cast<int>(model.scenes.size()))
    {
        for (int node : model.scenes[sceneIndex].nodes)
        {
            visit(node, glm::mat4(1.0f));
        }
    }
    else
    {
        for (int node = 0; node < static_cast<int>(model.nodes.size()); ++node)
        {
            visit(node, glm::mat4(1.0f));
        }
    }

    if (out.meshes.empty())
    {
        error = "glTF contains no triangle meshes";
        return false;
    }

    out.computeBounds();

    // Sponza 本身没有灯光：沿中庭补充五盏球形灯
    const glm::vec3 center = out.center();
    const glm::vec3 size = out.boundsMax - out.boundsMin;
    const float xs[5] = {-0.36f, -0.18f, 0.0f, 0.18f, 0.36f};
    const glm::vec3 colors[3] = {{48.0f, 40.0f, 30.0f}, {36.0f, 40.0f, 50.0f}, {48.0f, 40.0f, 30.0f}};

    for (int i = 0; i < 5; ++i)
    {
        Light light;
        light.type = 1;
        light.position = glm::vec3(center.x + size.x * xs[i], center.y + size.y * 0.12f, 0.0f);
        light.emission = colors[i % 3];
        light.radius = 1.0f;
        out.lights.push_back(light);
    }

    // 补充两盏彩色高亮球灯，用于体现 HDR 峰值与色溢
    Light warmAccent;
    warmAccent.type = 1;
    warmAccent.position = glm::vec3(center.x, center.y + size.y * 0.10f, center.z + size.z * 0.25f);
    warmAccent.emission = glm::vec3(80.0f, 50.0f, 25.0f);
    warmAccent.radius = 0.4f;
    out.lights.push_back(warmAccent);

    Light coolAccent;
    coolAccent.type = 1;
    coolAccent.position = glm::vec3(center.x, center.y + size.y * 0.10f, center.z - size.z * 0.25f);
    coolAccent.emission = glm::vec3(25.0f, 50.0f, 90.0f);
    coolAccent.radius = 0.4f;
    out.lights.push_back(coolAccent);

    RT_LOG("Loaded glTF: %zu meshes, %zu materials, %zu textures, %zu instances", out.meshes.size(),
           out.materials.size(), out.textures.size(), out.instances.size());
    return true;
}
