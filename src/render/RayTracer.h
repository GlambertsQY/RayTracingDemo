#pragma once

#include "render/GpuScene.h"
#include "render/RenderSettings.h"
#include "vulkan/Resources.h"

#include <volk.h>

#include <functional>

class Context;

class RayTracer
{
public:
    void init(Context& context, Resources& resources, VkFormat colorFormat);
    void shutdown();

    void setScene(const GpuScene& scene, VkExtent2D extent);
    void render(VkCommandBuffer cmd, const FrameConstants& constants, bool resetAccumulation,
                VkImage colorImage, VkImageView colorView,
                const std::function<void(VkCommandBuffer)>& overlay = nullptr);

    VkImage   accumImage() const { return m_accum.image; }
    VkExtent2D extent() const { return m_extent; }

private:
    void createDescriptors(const GpuScene& scene);
    void createSizeImages(VkExtent2D extent);
    void createPipelines(VkFormat colorFormat);
    void createSBT();

    VkShaderModule loadShader(const char* name) const;

    Context*   m_context = nullptr;
    Resources* m_resources = nullptr;

    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_pool = VK_NULL_HANDLE;
    VkDescriptorSet       m_set = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;

    VkPipeline m_rtPipeline = VK_NULL_HANDLE;
    VkPipeline m_accumulatePipeline = VK_NULL_HANDLE;
    VkPipeline m_tonemapPipeline = VK_NULL_HANDLE;

    Buffer m_sbt;
    Buffer m_constants;
    Image  m_radiance;
    Image  m_accum;

    VkStridedDeviceAddressRegionKHR m_raygenRegion{};
    VkStridedDeviceAddressRegionKHR m_missRegion{};
    VkStridedDeviceAddressRegionKHR m_hitRegion{};
    VkStridedDeviceAddressRegionKHR m_callableRegion{};

    VkExtent2D m_extent{0, 0};
    VkFormat   m_colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
    bool       m_sceneBound = false;
};
