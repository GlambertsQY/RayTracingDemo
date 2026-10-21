#include "render/RayTracer.h"

#include "common/Assets.h"
#include "common/Log.h"
#include "vulkan/Context.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
constexpr uint32_t kTextureCount = 128;

template <typename T>
T alignUp(T value, T alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}
} // namespace

VkShaderModule RayTracer::loadShader(const char* name) const
{
    std::vector<uint8_t> code;
    if (!assets::readShader(name, code))
    {
        RT_FATAL("Failed to load shader: %s", name);
    }
    if (code.empty() || code.size() % 4 != 0)
    {
        RT_FATAL("Invalid SPIR-V shader: %s", name);
    }

    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = code.size();
    info.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(m_context->device, &info, nullptr, &module) != VK_SUCCESS)
    {
        RT_FATAL("Failed to create shader module: %s", name);
    }
    return module;
}

void RayTracer::init(Context& context, Resources& resources, VkFormat colorFormat)
{
    m_context = &context;
    m_resources = &resources;
    m_colorFormat = colorFormat;

#if defined(__ANDROID__)
    // 部分移动驱动在 RT pipeline 着色器编译阶段直接 abort，Android 仅使用 ray query compute
    m_rayTracingAvailable = false;
#else
    m_rayTracingAvailable = context.rayTracingPipelineSupported;
#endif
    m_rayQueryAvailable = context.rayQuerySupported;

#if defined(__ANDROID__)
    m_backend = Backend::RayQueryCompute;
#else
    m_backend = m_rayTracingAvailable ? Backend::RayTracingPipeline : Backend::RayQueryCompute;
    const char* forceCompute = std::getenv("RTDEMO_COMPUTE");
    if (forceCompute && std::strcmp(forceCompute, "1") == 0)
    {
        m_backend = Backend::RayQueryCompute;
    }
#endif

    VkDescriptorSetLayoutBinding bindings[11]{};
    for (uint32_t i = 0; i < 11; ++i)
    {
        bindings[i].binding = i;
        bindings[i].descriptorCount = 1;
    }

    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[0].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR |
                             VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR |
                             VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    const VkShaderStageFlags geometryStages = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                                              VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT;
    for (uint32_t i = 2; i <= 7; ++i)
    {
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].stageFlags = geometryStages;
    }
    bindings[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[8].descriptorCount = kTextureCount;
    bindings[8].stageFlags = geometryStages;
    bindings[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[9].stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[10].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 11;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(context.device, &layoutInfo, nullptr, &m_setLayout) != VK_SUCCESS)
    {
        RT_FATAL("Failed to create descriptor set layout");
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_setLayout;
    if (vkCreatePipelineLayout(context.device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS)
    {
        RT_FATAL("Failed to create pipeline layout");
    }

    const VkDeviceSize constantSize = sizeof(FrameConstants);
    m_constants = resources.createBuffer(constantSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                         VMA_MEMORY_USAGE_CPU_TO_GPU, true);

    createPipelines(colorFormat);
}

void RayTracer::createPipelines(VkFormat colorFormat)
{
    VkDevice device = m_context->device;

    // ------------------------------------------------- ray tracing pipeline
    if (m_rayTracingAvailable)
    {
        VkShaderModule rgen = loadShader("path_tracer.rgen.spv");
        VkShaderModule rchit = loadShader("path_tracer.rchit.spv");
        VkShaderModule rmiss = loadShader("path_tracer.rmiss.spv");
        VkShaderModule rahit = loadShader("shadow.rahit.spv");
        VkShaderModule alphaAhit = loadShader("alpha.rahit.spv");
        VkShaderModule smiss = loadShader("shadow.rmiss.spv");

        VkPipelineShaderStageCreateInfo stages[6]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        stages[0].module = rgen;
        stages[0].pName = "main";

        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
        stages[1].module = rmiss;
        stages[1].pName = "main";

        stages[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[2].stage = VK_SHADER_STAGE_MISS_BIT_KHR;
        stages[2].module = smiss;
        stages[2].pName = "main";

        stages[3].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[3].stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
        stages[3].module = rchit;
        stages[3].pName = "main";

        stages[4].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[4].stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
        stages[4].module = rahit;
        stages[4].pName = "main";

        stages[5].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[5].stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
        stages[5].module = alphaAhit;
        stages[5].pName = "main";

        VkRayTracingShaderGroupCreateInfoKHR groups[5]{};
        for (int i = 0; i < 5; ++i)
        {
            groups[i].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
            groups[i].generalShader = VK_SHADER_UNUSED_KHR;
            groups[i].closestHitShader = VK_SHADER_UNUSED_KHR;
            groups[i].anyHitShader = VK_SHADER_UNUSED_KHR;
            groups[i].intersectionShader = VK_SHADER_UNUSED_KHR;
        }
        groups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[0].generalShader = 0;
        groups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[1].generalShader = 1;
        groups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[2].generalShader = 2;
        groups[3].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        groups[3].closestHitShader = 3;
        groups[3].anyHitShader = 5;
        groups[4].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        groups[4].anyHitShader = 4;

        VkRayTracingPipelineCreateInfoKHR pipelineInfo{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
        pipelineInfo.stageCount = 6;
        pipelineInfo.pStages = stages;
        pipelineInfo.groupCount = 5;
        pipelineInfo.pGroups = groups;
        pipelineInfo.maxPipelineRayRecursionDepth = 1;
        pipelineInfo.layout = m_pipelineLayout;

        if (vkCreateRayTracingPipelinesKHR(m_context->device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineInfo,
                                           nullptr, &m_rtPipeline) != VK_SUCCESS)
        {
            RT_LOG("Failed to create ray tracing pipeline; falling back to ray query compute");
            m_rayTracingAvailable = false;
            m_rtPipeline = VK_NULL_HANDLE;
            if (m_backend == Backend::RayTracingPipeline)
            {
                m_backend = Backend::RayQueryCompute;
            }
        }

        vkDestroyShaderModule(device, rgen, nullptr);
        vkDestroyShaderModule(device, rchit, nullptr);
        vkDestroyShaderModule(device, rmiss, nullptr);
        vkDestroyShaderModule(device, rahit, nullptr);
        vkDestroyShaderModule(device, alphaAhit, nullptr);
        vkDestroyShaderModule(device, smiss, nullptr);

        if (m_rtPipeline != VK_NULL_HANDLE)
        {
            createSBT();
        }
    }

    // ------------------------------------------------ ray query compute path
    if (m_rayQueryAvailable)
    {
        RT_LOG("Creating ray query compute pipeline");
        VkShaderModule module = loadShader("path_tracer.comp.spv");
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.module = module;
        info.stage.pName = "main";
        info.layout = m_pipelineLayout;
        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &m_rayQueryPipeline) != VK_SUCCESS)
        {
            RT_FATAL("Failed to create ray query compute pipeline");
        }
        vkDestroyShaderModule(device, module, nullptr);
        RT_LOG("Ray query compute pipeline ready");
    }

    // ------------------------------------------------------------- accumulate
    {
        VkShaderModule module = loadShader("accumulate.comp.spv");
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.module = module;
        info.stage.pName = "main";
        info.layout = m_pipelineLayout;
        if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &m_accumulatePipeline) != VK_SUCCESS)
        {
            RT_FATAL("Failed to create accumulate pipeline");
        }
        vkDestroyShaderModule(device, module, nullptr);
    }

    // ---------------------------------------------------------------- tonemap
    {
        VkShaderModule vert = loadShader("tonemap.vert.spv");
        VkShaderModule frag = loadShader("tonemap.frag.spv");

        VkPipelineShaderStageCreateInfo stageInfos[2]{};
        stageInfos[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfos[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stageInfos[0].module = vert;
        stageInfos[0].pName = "main";
        stageInfos[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfos[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stageInfos[1].module = frag;
        stageInfos[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &blendAttachment;

        VkDynamicState dynamicStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;

        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &renderingInfo;
        info.stageCount = 2;
        info.pStages = stageInfos;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &inputAssembly;
        info.pViewportState = &viewportState;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depthStencil;
        info.pColorBlendState = &colorBlend;
        info.pDynamicState = &dynamicState;
        info.layout = m_pipelineLayout;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &m_tonemapPipeline) != VK_SUCCESS)
        {
            RT_FATAL("Failed to create tonemap pipeline");
        }

        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
    }

    RT_LOG("Ray tracing backend: %s", backendName());
}

void RayTracer::createSBT()
{
    const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& rtProps = m_context->rtProps;
    const uint32_t handleSize = rtProps.shaderGroupHandleSize;
    const uint32_t handleAlignment = rtProps.shaderGroupHandleAlignment;
    const uint32_t baseAlignment = rtProps.shaderGroupBaseAlignment;

    const uint32_t recordSize = alignUp(handleSize, handleAlignment);
    const VkDeviceSize raygenSize = alignUp<uint32_t>(handleSize, baseAlignment);
    const VkDeviceSize missOffset = raygenSize;
    const VkDeviceSize missSize = 2 * recordSize;
    const VkDeviceSize hitOffset = alignUp<VkDeviceSize>(missOffset + missSize, baseAlignment);
    const VkDeviceSize hitSize = 2 * recordSize;
    const VkDeviceSize totalSize = alignUp<VkDeviceSize>(hitOffset + hitSize, baseAlignment);

    std::vector<uint8_t> handles(5 * handleSize);
    if (vkGetRayTracingShaderGroupHandlesKHR(m_context->device, m_rtPipeline, 0, 5,
                                             static_cast<size_t>(handles.size()), handles.data()) != VK_SUCCESS)
    {
        RT_FATAL("vkGetRayTracingShaderGroupHandlesKHR failed");
    }

    m_sbt = m_resources->createBuffer(totalSize,
                                      VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                                          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                      VMA_MEMORY_USAGE_CPU_ONLY, true);

    auto* data = static_cast<uint8_t*>(m_sbt.mapped);
    std::memset(data, 0, static_cast<size_t>(totalSize));
    std::memcpy(data, handles.data(), handleSize);
    std::memcpy(data + missOffset + 0 * recordSize, handles.data() + 1 * handleSize, handleSize);
    std::memcpy(data + missOffset + 1 * recordSize, handles.data() + 2 * handleSize, handleSize);
    std::memcpy(data + hitOffset + 0 * recordSize, handles.data() + 3 * handleSize, handleSize);
    std::memcpy(data + hitOffset + 1 * recordSize, handles.data() + 4 * handleSize, handleSize);

    m_raygenRegion = {m_sbt.address, raygenSize, raygenSize};
    m_missRegion = {m_sbt.address + missOffset, missSize, recordSize};
    m_hitRegion = {m_sbt.address + hitOffset, hitSize, recordSize};
    m_callableRegion = {};
}

void RayTracer::createSizeImages(VkExtent2D extent)
{
    m_resources->destroyImage(m_radiance);
    m_resources->destroyImage(m_accum);

    m_radiance = m_resources->createImage2D(VK_FORMAT_R16G16B16A16_SFLOAT, extent,
                                            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                                VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    m_accum = m_resources->createImage2D(VK_FORMAT_R32G32B32A32_SFLOAT, extent,
                                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                             VK_IMAGE_USAGE_TRANSFER_DST_BIT);

    m_resources->immediate([&](VkCommandBuffer cmd) {
        transitionImage(cmd, m_radiance.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        transitionImage(cmd, m_accum.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    });

    m_extent = extent;
}

void RayTracer::createDescriptors(const GpuScene& scene)
{
    if (m_pool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(m_context->device, m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
        m_set = VK_NULL_HANDLE;
    }

    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kTextureCount},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2},
    };

    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(sizes));
    poolInfo.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(m_context->device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS)
    {
        RT_FATAL("Failed to create descriptor pool");
    }

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = m_pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_setLayout;
    if (vkAllocateDescriptorSets(m_context->device, &allocInfo, &m_set) != VK_SUCCESS)
    {
        RT_FATAL("Failed to allocate descriptor set");
    }

    VkWriteDescriptorSetAccelerationStructureKHR asWrite{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    asWrite.accelerationStructureCount = 1;
    asWrite.pAccelerationStructures = &scene.tlas.handle;

    VkDescriptorBufferInfo constantsInfo{m_constants.buffer, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo bufferInfos[6] = {
        {scene.vertexBuffer.buffer, 0, VK_WHOLE_SIZE},
        {scene.indexBuffer.buffer, 0, VK_WHOLE_SIZE},
        {scene.meshBuffer.buffer, 0, VK_WHOLE_SIZE},
        {scene.instanceBuffer.buffer, 0, VK_WHOLE_SIZE},
        {scene.materialBuffer.buffer, 0, VK_WHOLE_SIZE},
        {scene.lightBuffer.buffer, 0, VK_WHOLE_SIZE},
    };

    std::vector<VkDescriptorImageInfo> textureInfos(kTextureCount);
    const VkDescriptorImageInfo fallback{scene.sampler, scene.textures.empty() ? VK_NULL_HANDLE : scene.textures[0].view,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    for (auto& info : textureInfos)
    {
        info = fallback;
    }
    for (size_t i = 0; i < std::min<size_t>(scene.textures.size(), kTextureCount); ++i)
    {
        textureInfos[i] = {scene.sampler, scene.textures[i].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    }

    VkDescriptorImageInfo radianceInfo{VK_NULL_HANDLE, m_radiance.view, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo accumInfo{VK_NULL_HANDLE, m_accum.view, VK_IMAGE_LAYOUT_GENERAL};

    VkWriteDescriptorSet writes[11]{};
    for (uint32_t i = 0; i < 11; ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = m_set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
    }

    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    writes[0].pNext = &asWrite;

    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].pBufferInfo = &constantsInfo;

    for (uint32_t i = 0; i < 6; ++i)
    {
        writes[2 + i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2 + i].pBufferInfo = &bufferInfos[i];
    }

    writes[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[8].descriptorCount = kTextureCount;
    writes[8].pImageInfo = textureInfos.data();

    writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[9].pImageInfo = &radianceInfo;

    writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[10].pImageInfo = &accumInfo;

    vkUpdateDescriptorSets(m_context->device, 11, writes, 0, nullptr);
    m_sceneBound = true;
}

void RayTracer::setScene(const GpuScene& scene, VkExtent2D renderExtent, VkExtent2D outputExtent)
{
    vkDeviceWaitIdle(m_context->device);

    if (renderExtent.width != m_extent.width || renderExtent.height != m_extent.height ||
        m_radiance.image == VK_NULL_HANDLE)
    {
        createSizeImages(renderExtent);
    }
    m_outputExtent = outputExtent;
    createDescriptors(scene);
}

void RayTracer::setBackend(Backend backend)
{
    if (backendAvailable(backend))
    {
        m_backend = backend;
    }
}

bool RayTracer::backendAvailable(Backend backend) const
{
    if (backend == Backend::RayTracingPipeline)
    {
        return m_rayTracingAvailable && m_rtPipeline != VK_NULL_HANDLE;
    }
    return m_rayQueryAvailable && m_rayQueryPipeline != VK_NULL_HANDLE;
}

const char* RayTracer::backendName() const
{
    return m_backend == Backend::RayTracingPipeline ? "RayTracingPipeline" : "RayQueryCompute";
}

void RayTracer::render(VkCommandBuffer cmd, const FrameConstants& constants, bool resetAccumulation,
                       VkImage colorImage, VkImageView colorView,
                       const std::function<void(VkCommandBuffer)>& overlay)
{
    if (!m_sceneBound)
    {
        return;
    }

    std::memcpy(m_constants.mapped, &constants, sizeof(FrameConstants));

    if (resetAccumulation)
    {
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkClearColorValue zero{};
        vkCmdClearColorImage(cmd, m_radiance.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
        vkCmdClearColorImage(cmd, m_accum.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);

        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                             1, &barrier, 0, nullptr, 0, nullptr);
    }

    const bool useRayTracing = m_backend == Backend::RayTracingPipeline && backendAvailable(Backend::RayTracingPipeline);
    const bool useRayQuery = !useRayTracing && backendAvailable(Backend::RayQueryCompute);

    if (useRayTracing)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_rtPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipelineLayout, 0, 1, &m_set, 0,
                                nullptr);
        vkCmdTraceRaysKHR(cmd, &m_raygenRegion, &m_missRegion, &m_hitRegion, &m_callableRegion, m_extent.width,
                          m_extent.height, 1);
    }
    else if (useRayQuery)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_rayQueryPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_set, 0, nullptr);
        vkCmdDispatch(cmd, (m_extent.width + 7) / 8, (m_extent.height + 7) / 8, 1);
    }
    else
    {
        return;
    }

    const VkPipelineStageFlags traceStage =
        useRayTracing ? VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

    {
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, traceStage, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0,
                             nullptr);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_accumulatePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_set, 0, nullptr);
    vkCmdDispatch(cmd, (m_extent.width + 7) / 8, (m_extent.height + 7) / 8, 1);

    {
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1,
                             &barrier, 0, nullptr, 0, nullptr);
    }

    transitionImage(cmd, colorImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    const VkExtent2D outputExtent =
        m_outputExtent.width > 0 && m_outputExtent.height > 0 ? m_outputExtent : m_extent;

    VkRenderingAttachmentInfo colorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    colorAttachment.imageView = colorView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}};

    VkRenderingInfo renderInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderInfo.renderArea = {{0, 0}, outputExtent};
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(cmd, &renderInfo);

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(outputExtent.width),
                        static_cast<float>(outputExtent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, outputExtent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tonemapPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_set, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    if (overlay)
    {
        overlay(cmd);
    }

    vkCmdEndRendering(cmd);
}

void RayTracer::shutdown()
{
    if (!m_context)
    {
        return;
    }

    VkDevice device = m_context->device;
    vkDeviceWaitIdle(device);

    if (m_rtPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, m_rtPipeline, nullptr);
        m_rtPipeline = VK_NULL_HANDLE;
    }
    if (m_rayQueryPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, m_rayQueryPipeline, nullptr);
        m_rayQueryPipeline = VK_NULL_HANDLE;
    }
    if (m_accumulatePipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, m_accumulatePipeline, nullptr);
        m_accumulatePipeline = VK_NULL_HANDLE;
    }
    if (m_tonemapPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, m_tonemapPipeline, nullptr);
        m_tonemapPipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_pool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
        m_set = VK_NULL_HANDLE;
    }
    if (m_setLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
        m_setLayout = VK_NULL_HANDLE;
    }

    m_resources->destroyBuffer(m_sbt);
    m_resources->destroyBuffer(m_constants);
    m_resources->destroyImage(m_radiance);
    m_resources->destroyImage(m_accum);

    m_context = nullptr;
    m_resources = nullptr;
}
