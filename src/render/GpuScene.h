#pragma once

#include "render/AccelerationStructure.h"
#include "render/RenderSettings.h"
#include "scene/Scene.h"
#include "vulkan/Resources.h"

#include <volk.h>
#include <vector>

class Context;

class GpuScene
{
public:
    void create(Context& context, Resources& resources, const Scene& scene);
    void destroy();

    Buffer vertexBuffer;
    Buffer indexBuffer;
    Buffer meshBuffer;
    Buffer instanceBuffer;
    Buffer materialBuffer;
    Buffer lightBuffer;

    std::vector<Image> textures;      // [0] = 白色, [1] = 平坦法线
    VkSampler sampler = VK_NULL_HANDLE;

    std::vector<AccelStructure> blas;
    AccelStructure tlas;

    uint32_t triangleCount = 0;
    uint32_t instanceCount = 0;
    uint32_t lightCount = 0;
    uint32_t textureCount = 0;

private:
    Context* m_context = nullptr;
    Resources* m_resources = nullptr;
};
