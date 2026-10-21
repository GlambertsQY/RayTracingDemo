#include "render/GpuScene.h"

#include "common/Log.h"
#include "vulkan/Context.h"

#include <algorithm>
#include <cstring>

namespace
{
VkTransformMatrixKHR toTransformMatrix(const glm::mat4& m)
{
    VkTransformMatrixKHR result{};
    for (int row = 0; row < 3; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            result.matrix[row][column] = m[column][row];
        }
    }
    return result;
}
} // namespace

void GpuScene::create(Context& context, Resources& resources, const Scene& scene)
{
    m_context = &context;
    m_resources = &resources;
    triangleCount = 0;

    // -------------------------------------------------------------- 合并几何
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<GpuMesh> meshes;
    meshes.reserve(scene.meshes.size());

    for (const Mesh& mesh : scene.meshes)
    {
        GpuMesh gpuMesh{};
        gpuMesh.firstIndex = static_cast<uint32_t>(indices.size());
        gpuMesh.indexCount = static_cast<uint32_t>(mesh.indices.size());
        gpuMesh.baseVertex = static_cast<int32_t>(vertices.size());
        gpuMesh.materialIndex = static_cast<uint32_t>(mesh.material);
        meshes.push_back(gpuMesh);

        vertices.insert(vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
        for (uint32_t index : mesh.indices)
        {
            indices.push_back(index + static_cast<uint32_t>(gpuMesh.baseVertex));
        }
        triangleCount += static_cast<uint32_t>(mesh.indices.size() / 3);
    }

    vertexBuffer = resources.createBuffer(
        std::max<VkDeviceSize>(1, vertices.size() * sizeof(Vertex)),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VMA_MEMORY_USAGE_CPU_ONLY, true);
    resources.uploadBuffer(vertexBuffer, vertices.data(), vertices.size() * sizeof(Vertex));

    indexBuffer = resources.createBuffer(
        std::max<VkDeviceSize>(1, indices.size() * sizeof(uint32_t)),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VMA_MEMORY_USAGE_CPU_ONLY, true);
    resources.uploadBuffer(indexBuffer, indices.data(), indices.size() * sizeof(uint32_t));

    meshBuffer = resources.createBuffer(std::max<VkDeviceSize>(1, meshes.size() * sizeof(GpuMesh)),
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_ONLY, true);
    resources.uploadBuffer(meshBuffer, meshes.data(), meshes.size() * sizeof(GpuMesh));

    // -------------------------------------------------------------- 实例/材质
    std::vector<GpuInstance> gpuInstances;
    gpuInstances.reserve(scene.instances.size());
    for (const Instance& inst : scene.instances)
    {
        GpuInstance g{};
        g.meshIndex = static_cast<uint32_t>(inst.mesh);
        g.materialOverride = inst.material;
        gpuInstances.push_back(g);
    }
    instanceCount = static_cast<uint32_t>(gpuInstances.size());

    instanceBuffer = resources.createBuffer(std::max<VkDeviceSize>(1, gpuInstances.size() * sizeof(GpuInstance)),
                                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_ONLY, true);
    resources.uploadBuffer(instanceBuffer, gpuInstances.data(), gpuInstances.size() * sizeof(GpuInstance));

    std::vector<GpuMaterial> gpuMaterials;
    gpuMaterials.reserve(scene.materials.size());
    for (const Material& mat : scene.materials)
    {
        GpuMaterial g{};
        g.baseColor = glm::vec4(mat.baseColor, 1.0f);
        g.emissive = glm::vec4(mat.emissive, mat.transmission > 0.5f ? 1.0f : 0.0f);
        g.metallic = mat.metallic;
        g.roughness = glm::clamp(mat.roughness, 0.02f, 1.0f);
        g.ior = mat.ior;
        g.alphaCutoff = mat.alphaCutoff;
        g.baseColorTex = mat.baseColorTex >= 0 ? mat.baseColorTex + 2 : 0;
        g.normalTex = mat.normalTex >= 0 ? mat.normalTex + 2 : 1;
        g.mrTex = mat.mrTex >= 0 ? mat.mrTex + 2 : 0;
        g.alphaMode = mat.alphaMode == 1 ? 1 : 0;
        gpuMaterials.push_back(g);
    }
    if (gpuMaterials.empty())
    {
        gpuMaterials.push_back(GpuMaterial{});
    }

    materialBuffer = resources.createBuffer(gpuMaterials.size() * sizeof(GpuMaterial),
                                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_ONLY, true);
    resources.uploadBuffer(materialBuffer, gpuMaterials.data(), gpuMaterials.size() * sizeof(GpuMaterial));

    std::vector<GpuLight> gpuLights;
    gpuLights.reserve(scene.lights.size());
    for (const Light& light : scene.lights)
    {
        GpuLight g{};
        g.position = glm::vec4(light.position, static_cast<float>(light.type));
        g.emission = glm::vec4(light.emission, light.radius);
        g.u = glm::vec4(light.u, light.innerCos);
        g.v = glm::vec4(light.v, light.outerCos);
        gpuLights.push_back(g);
    }
    lightCount = static_cast<uint32_t>(gpuLights.size());
    if (gpuLights.empty())
    {
        gpuLights.push_back(GpuLight{});
    }

    lightBuffer = resources.createBuffer(gpuLights.size() * sizeof(GpuLight),
                                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_ONLY, true);
    resources.uploadBuffer(lightBuffer, gpuLights.data(), gpuLights.size() * sizeof(GpuLight));

    // ------------------------------------------------------------------ 纹理
    const uint8_t white[4] = {255, 255, 255, 255};
    const uint8_t flatNormal[4] = {128, 128, 255, 255};
    textures.push_back(resources.createTextureImage(VK_FORMAT_R8G8B8A8_UNORM, {1, 1}, white, sizeof(white), false));
    textures.push_back(resources.createTextureImage(VK_FORMAT_R8G8B8A8_UNORM, {1, 1}, flatNormal,
                                                    sizeof(flatNormal), false));

    for (const TextureData& data : scene.textures)
    {
        const VkFormat format = data.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        textures.push_back(resources.createTextureImage(
            format, {static_cast<uint32_t>(data.width), static_cast<uint32_t>(data.height)}, data.pixels.data(),
            data.pixels.size(), true));
    }
    textureCount = static_cast<uint32_t>(textures.size());
    sampler = resources.createSampler(8);

    // ------------------------------------------------------------ 加速结构
    blas.resize(scene.meshes.size());
    for (size_t i = 0; i < scene.meshes.size(); ++i)
    {
        const GpuMesh& gpuMesh = meshes[i];
        if (gpuMesh.indexCount == 0)
        {
            continue;
        }
        const VkDeviceAddress vertexAddress = vertexBuffer.address;
        const VkDeviceAddress indexAddress =
            indexBuffer.address + static_cast<VkDeviceAddress>(gpuMesh.firstIndex) * sizeof(uint32_t);
        blas[i].buildBLAS(context, resources, vertexAddress, static_cast<uint32_t>(vertices.size()), indexAddress,
                          gpuMesh.indexCount, sizeof(Vertex));
    }

    std::vector<VkAccelerationStructureInstanceKHR> asInstances;
    asInstances.reserve(scene.instances.size());
    for (size_t i = 0; i < scene.instances.size(); ++i)
    {
        const Instance& inst = scene.instances[i];
        if (inst.mesh < 0 || inst.mesh >= static_cast<int>(blas.size()) || blas[inst.mesh].handle == VK_NULL_HANDLE)
        {
            continue;
        }

        VkAccelerationStructureDeviceAddressInfoKHR addressInfo{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
        addressInfo.accelerationStructure = blas[inst.mesh].handle;

        VkAccelerationStructureInstanceKHR asInstance{};
        asInstance.transform = toTransformMatrix(inst.transform);
        asInstance.instanceCustomIndex = static_cast<uint32_t>(i);
        asInstance.mask = 0xFF;
        asInstance.instanceShaderBindingTableRecordOffset = 0;
        asInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        asInstance.accelerationStructureReference =
            vkGetAccelerationStructureDeviceAddressKHR(context.device, &addressInfo);
        asInstances.push_back(asInstance);
    }

    tlas.buildTLAS(context, resources, asInstances);

    RT_LOG("GpuScene: %zu meshes, %u instances, %u triangles, %zu textures, %u lights", scene.meshes.size(),
           instanceCount, triangleCount, textures.size(), lightCount);
}

void GpuScene::destroy()
{
    if (!m_context)
    {
        return;
    }

    tlas.destroy(*m_context);
    for (auto& structure : blas)
    {
        structure.destroy(*m_context);
    }
    blas.clear();

    m_resources->destroyBuffer(vertexBuffer);
    m_resources->destroyBuffer(indexBuffer);
    m_resources->destroyBuffer(meshBuffer);
    m_resources->destroyBuffer(instanceBuffer);
    m_resources->destroyBuffer(materialBuffer);
    m_resources->destroyBuffer(lightBuffer);

    for (auto& texture : textures)
    {
        m_resources->destroyImage(texture);
    }
    textures.clear();

    if (sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(m_context->device, sampler, nullptr);
        sampler = VK_NULL_HANDLE;
    }

    m_context = nullptr;
}
