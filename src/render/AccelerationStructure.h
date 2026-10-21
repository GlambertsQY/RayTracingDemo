#pragma once

#include "vulkan/Resources.h"

#include <volk.h>
#include <vector>

class Context;

class AccelStructure
{
public:
    void buildBLAS(Context& context, Resources& resources, VkDeviceAddress vertexAddress, uint32_t vertexCount,
                   VkDeviceAddress indexAddress, uint32_t indexCount, uint32_t vertexStride);
    void buildTLAS(Context& context, Resources& resources,
                   const std::vector<VkAccelerationStructureInstanceKHR>& instances);
    void destroy(Context& context);

    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    Buffer buffer;

private:
    Buffer m_scratch;
};
