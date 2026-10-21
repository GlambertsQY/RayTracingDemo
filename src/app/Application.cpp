#include "app/Application.h"

#include "common/Assets.h"
#include "common/Log.h"
#include "platform/Platform.h"
#include "scene/GltfLoader.h"
#include "scene/TextureLoader.h"

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <vector>

#ifdef RTDEMO_HAVE_RENDERDOC
#include <windows.h>
#endif

namespace
{
bool renderSettingsDiffer(const Settings& a, const Settings& b)
{
    return a.maxBounces != b.maxBounces || a.neeBounces != b.neeBounces ||
           a.samplesPerFrame != b.samplesPerFrame || a.exposure != b.exposure ||
           a.sunIntensity != b.sunIntensity || a.sunAzimuth != b.sunAzimuth || a.sunElevation != b.sunElevation ||
           a.envIntensity != b.envIntensity || a.fireflyClamp != b.fireflyClamp ||
           a.fogDensity != b.fogDensity || a.fogAlbedo != b.fogAlbedo || a.fogAnisotropy != b.fogAnisotropy;
}
} // namespace

bool Application::init(Platform& platform, const Options& options)
{
    m_platform = &platform;
    m_options = options;
    m_settings.sceneIndex = options.scene;
    m_settings.showUI = options.screenshot.empty();

#if defined(__ANDROID__)
    m_settings.maxBounces = 2;
    m_settings.neeBounces = 1;
    m_settings.samplesPerFrame = 1;
    m_settings.renderScale = 0.5f;
    m_settings.fireflyClamp = 20.0f;
    // 请求 HDR 输出；实际是否可用由交换链格式决定，失败时回退 SDR
    m_settings.hdrOutput = platform.hdrOutputSupported() && platform.setHdrOutput(true);
    // 按屏幕实际 HDR 峰值亮度初始化（未知时保持 1000 nits）
    const float displayPeakNits = platform.hdrDisplayPeakNits();
    if (displayPeakNits > 0.0f)
    {
        m_settings.hdrPeakNits = std::clamp(displayPeakNits, 400.0f, 4000.0f);
    }
#endif

#ifdef RTDEMO_HAVE_RENDERDOC
    if (options.renderDocCapture)
    {
        HMODULE module = GetModuleHandleA("renderdoc.dll");
        if (!module)
        {
            module = LoadLibraryA("renderdoc.dll");
        }
        if (module)
        {
            auto getApi = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"));
            if (getApi)
            {
                void* api = nullptr;
                if (getApi(eRENDERDOC_API_Version_1_7_0, &api) == 1 && api)
                {
                    m_renderDoc = static_cast<RENDERDOC_API_1_7_0*>(api);
                    std::error_code ec;
                    std::filesystem::create_directories("captures", ec);
                    m_renderDoc->SetCaptureFilePathTemplate("captures/rdoc");
                    RT_LOG("RenderDoc capture enabled");
                }
            }
        }
        if (!m_renderDoc)
        {
            RT_LOG("RenderDoc API unavailable; launch through renderdoccmd");
        }
    }
#endif

    if (!m_context.init(platform))
    {
        RT_LOG("Vulkan context initialization failed");
        m_context.shutdown();
        return false;
    }

    m_resources.init(m_context);

    initSwapchain();

    createFrameResources();
    m_rayTracer.init(m_context, m_resources, m_swapchain.format);
    m_ui.init(m_context, platform, m_swapchain.format, m_swapchain.imageCount(), m_swapchain.hdr());
    m_uiInitialized = true;
    platform.setImGuiInputEnabled(true);

    m_camera.aspect =
        static_cast<float>(m_swapchain.extent.width) / static_cast<float>(std::max(m_swapchain.extent.height, 1u));
    updateScene(m_settings.sceneIndex);

    m_lastTime = std::chrono::steady_clock::now();
    return true;
}

void Application::initSwapchain()
{
    if (m_platform)
    {
        m_platform->getFramebufferSize(m_width, m_height);
    }

    // 先记录峰值亮度，交换链创建时会一并写入 HDR metadata
    m_swapchain.setHdrMetadata(m_context, m_settings.hdrPeakNits);
    m_swapchain.init(m_context, m_width, m_height, m_settings.hdrOutput);

    if (m_settings.hdrOutput && !m_swapchain.hdr())
    {
        RT_LOG("HDR output not supported by surface, falling back to SDR");
        m_settings.hdrOutput = false;
        if (m_platform)
        {
            m_platform->setHdrOutput(false);
        }
        m_swapchain.recreate(m_context, m_width, m_height, false);
    }

    RT_LOG("Output: %s", m_swapchain.hdr() ? "HDR10 (PQ/BT.2020)" : "SDR (sRGB)");
}

void Application::applyHdrToggle()
{
    m_pendingHdrToggle = false;
    if (!m_platform)
    {
        return;
    }

    if (m_settings.hdrOutput)
    {
        if (!m_platform->hdrOutputSupported() || !m_platform->setHdrOutput(true))
        {
            RT_LOG("HDR output request failed");
            m_settings.hdrOutput = false;
            return;
        }
    }
    else
    {
        m_platform->setHdrOutput(false);
    }

    // 切换 dataspace 需要重建 surface 与交换链（复用 SurfaceLost 路径）
    suspend();
    resume();
}

void Application::createFrameResources()
{
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_context.graphicsFamily;
    if (vkCreateCommandPool(m_context.device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS)
    {
        RT_FATAL("Failed to create frame command pool");
    }

    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    vkAllocateCommandBuffers(m_context.device, &allocInfo, &m_commandBuffer);

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(m_context.device, &fenceInfo, nullptr, &m_frameFence);

    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vkCreateSemaphore(m_context.device, &semaphoreInfo, nullptr, &m_imageAvailable);
    m_renderFinished.resize(m_swapchain.imageCount());
    for (VkSemaphore& semaphore : m_renderFinished)
    {
        vkCreateSemaphore(m_context.device, &semaphoreInfo, nullptr, &semaphore);
    }

#if !defined(__ANDROID__)
    const VkDeviceSize readbackSize = static_cast<VkDeviceSize>(m_swapchain.extent.width) *
                                      m_swapchain.extent.height * 4;
    m_readback = m_resources.createBuffer(readbackSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_ONLY,
                                          true);
#endif
}

void Application::destroyFrameResources()
{
    m_resources.destroyBuffer(m_readback);

    if (m_frameFence != VK_NULL_HANDLE)
    {
        vkDestroyFence(m_context.device, m_frameFence, nullptr);
        m_frameFence = VK_NULL_HANDLE;
    }
    if (m_imageAvailable != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(m_context.device, m_imageAvailable, nullptr);
        m_imageAvailable = VK_NULL_HANDLE;
    }
    for (VkSemaphore semaphore : m_renderFinished)
    {
        vkDestroySemaphore(m_context.device, semaphore, nullptr);
    }
    m_renderFinished.clear();
    if (m_commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(m_context.device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
        m_commandBuffer = VK_NULL_HANDLE;
    }
}

void Application::loadSceneIfNeeded(int sceneIndex)
{
    if (sceneIndex < 0 || sceneIndex > 3 || m_sceneLoaded[sceneIndex])
    {
        return;
    }

    if (sceneIndex == 1)
    {
        const std::string path = "Sponza/Sponza.gltf";
        std::string error;
        if (assets::exists(path) && loadGltfScene(path, m_scenes[1], error))
        {
            m_sceneLoaded[1] = true;
            return;
        }

        RT_LOG("Sponza unavailable (%s), falling back to procedural scene",
               error.empty() ? "asset not found" : error.c_str());
    }
    else if (sceneIndex == 2)
    {
        m_scenes[2] = buildShowcaseScene();
        m_sceneLoaded[2] = true;
        return;
    }
    else if (sceneIndex == 3)
    {
        m_scenes[3] = buildSunlitGalleryScene();
        m_sceneLoaded[3] = true;
        return;
    }

    m_scenes[0] = buildProceduralScene();
    m_sceneLoaded[0] = true;
    if (sceneIndex == 1)
    {
        m_settings.sceneIndex = 0;
    }
}

void Application::updateRenderExtent()
{
    const float scale = std::clamp(m_settings.renderScale, 0.25f, 1.0f);
    m_renderWidth = std::max(1u, static_cast<uint32_t>(static_cast<float>(m_swapchain.extent.width) * scale));
    m_renderHeight = std::max(1u, static_cast<uint32_t>(static_cast<float>(m_swapchain.extent.height) * scale));
}

void Application::updateScene(int sceneIndex)
{
    loadSceneIfNeeded(sceneIndex);

    const int actualScene = m_settings.sceneIndex;
    const Scene& scene = m_scenes[actualScene];

    // 每个场景的默认观感参数（夕阳场景带体积雾，其他场景关闭）
    if (actualScene == 2)
    {
        m_settings.fogDensity = 0.015f;
        m_settings.fogAlbedo = 0.90f;
        m_settings.fogAnisotropy = 0.78f;
        m_settings.sunAzimuth = 55.0f;
        m_settings.sunElevation = 8.0f;
        m_settings.sunIntensity = 16.0f;
        m_settings.envIntensity = 0.3f;
        m_settings.fireflyClamp = 500.0f;
#if defined(__ANDROID__)
        m_settings.maxBounces = 3;
        m_settings.neeBounces = 1;
#endif
    }
    else if (actualScene == 3)
    {
        // 阳光展厅：天窗百叶 + 轻雾，光照丰富但几何/光源数量都很小
        m_settings.fogDensity = 0.010f;
        m_settings.fogAlbedo = 0.88f;
        m_settings.fogAnisotropy = 0.55f;
        m_settings.sunAzimuth = 25.0f;
        m_settings.sunElevation = 32.0f;
        m_settings.sunIntensity = 11.0f;
        m_settings.envIntensity = 0.12f;
        m_settings.fireflyClamp = 50.0f;
#if defined(__ANDROID__)
        m_settings.maxBounces = 3;
        m_settings.neeBounces = 1;
#endif
    }
    else
    {
        m_settings.fogDensity = 0.0f;
        m_settings.sunAzimuth = 40.0f;
        m_settings.sunElevation = 45.0f;
        m_settings.sunIntensity = 4.0f;
        m_settings.envIntensity = 0.9f;
#if defined(__ANDROID__)
        m_settings.fireflyClamp = 20.0f;
        m_settings.maxBounces = 2;
        m_settings.neeBounces = 1;
#else
        m_settings.fireflyClamp = 5.0f;
#endif
    }
    m_fogDistance = std::max(scene.radius() * (actualScene >= 2 ? 1.15f : 3.0f), 12.0f);

    m_gpuScene.destroy();
    m_gpuScene.create(m_context, m_resources, scene);

    updateRenderExtent();
    m_rayTracer.setScene(m_gpuScene, {m_renderWidth, m_renderHeight}, m_swapchain.extent);

    m_camera.reset(scene.center(), scene.radius());
    if (actualScene == 1)
    {
        // Sponza 需要把相机放到建筑内部
        const glm::vec3 size = scene.boundsMax - scene.boundsMin;
        m_camera.distance = std::max(1.0f, size.x * 0.12f);
        m_camera.target = scene.center();
        m_camera.yaw = glm::radians(-90.0f);
        m_camera.pitch = 0.0f;
    }
    else if (actualScene == 2)
    {
        // 站在展厅内部，朝开放口方向看夕阳与体积光柱
        m_camera.target = glm::vec3(0.0f, 3.2f, 1.0f);
        m_camera.distance = 10.5f;
        m_camera.yaw = glm::radians(180.0f);
        m_camera.pitch = glm::radians(0.0f);
    }
    else if (actualScene == 3)
    {
        // 站在阳光展厅后部，朝开放的正面看百叶光柱与展品
        m_camera.target = glm::vec3(0.0f, 2.3f, 2.0f);
        m_camera.distance = 9.0f;
        m_camera.yaw = glm::radians(180.0f);
        m_camera.pitch = glm::radians(5.0f);
    }
    m_camera.aspect =
        static_cast<float>(m_swapchain.extent.width) / static_cast<float>(std::max(m_swapchain.extent.height, 1u));
    m_lastCameraPos = m_camera.position();
    m_lastFov = m_camera.fovY;

    RT_LOG("Scene %d camera pos (%.2f, %.2f, %.2f) target (%.2f, %.2f, %.2f) dist %.2f yaw %.2f",
           actualScene, m_lastCameraPos.x, m_lastCameraPos.y, m_lastCameraPos.z, m_camera.target.x,
           m_camera.target.y, m_camera.target.z, m_camera.distance, m_camera.yaw);

    m_activeScene = actualScene;
    m_accumulatedFrames = 0;
    m_resetAccumulation = true;
}

void Application::recreateSwapchain(bool force)
{
    if (m_suspended || m_swapchain.handle() == VK_NULL_HANDLE)
    {
        return;
    }

    m_platform->getFramebufferSize(m_width, m_height);
    if (m_width == 0 || m_height == 0)
    {
        return;
    }
    if (!force && m_width == m_swapchain.extent.width && m_height == m_swapchain.extent.height)
    {
        return;
    }

    vkDeviceWaitIdle(m_context.device);
    m_swapchain.recreate(m_context, m_width, m_height, m_settings.hdrOutput);
    m_rayTracer.setColorFormat(m_swapchain.format);

    for (VkSemaphore semaphore : m_renderFinished)
    {
        vkDestroySemaphore(m_context.device, semaphore, nullptr);
    }
    m_renderFinished.clear();
    m_renderFinished.resize(m_swapchain.imageCount());
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (VkSemaphore& semaphore : m_renderFinished)
    {
        vkCreateSemaphore(m_context.device, &semaphoreInfo, nullptr, &semaphore);
    }

#if !defined(__ANDROID__)
    m_resources.destroyBuffer(m_readback);
    const VkDeviceSize readbackSize =
        static_cast<VkDeviceSize>(m_swapchain.extent.width) * m_swapchain.extent.height * 4;
    m_readback = m_resources.createBuffer(readbackSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_ONLY,
                                          true);
#endif

    if (m_activeScene >= 0)
    {
        updateRenderExtent();
        m_rayTracer.setScene(m_gpuScene, {m_renderWidth, m_renderHeight}, m_swapchain.extent);
    }
    m_camera.aspect =
        static_cast<float>(m_swapchain.extent.width) / static_cast<float>(std::max(m_swapchain.extent.height, 1u));
    m_accumulatedFrames = 0;
    m_resetAccumulation = true;
}

void Application::suspend()
{
    if (m_suspended || m_context.device == VK_NULL_HANDLE)
    {
        return;
    }

    RT_LOG("Application suspend");

    vkDeviceWaitIdle(m_context.device);
    if (m_uiInitialized)
    {
        m_platform->setImGuiInputEnabled(false);
        m_ui.shutdown();
        m_uiInitialized = false;
    }
    destroyFrameResources();
    m_swapchain.shutdown(m_context);
    m_context.destroySurface();
    m_suspended = true;
}

void Application::resume()
{
    if (!m_suspended)
    {
        return;
    }

    if (!m_context.createSurface(*m_platform))
    {
        RT_LOG("Failed to recreate Android surface");
        return;
    }

    m_platform->getFramebufferSize(m_width, m_height);
    initSwapchain();
    createFrameResources();
    m_rayTracer.setColorFormat(m_swapchain.format);
    m_ui.init(m_context, *m_platform, m_swapchain.format, m_swapchain.imageCount(), m_swapchain.hdr());
    m_uiInitialized = true;
    m_platform->setImGuiInputEnabled(true);

    if (m_activeScene >= 0)
    {
        updateRenderExtent();
        m_rayTracer.setScene(m_gpuScene, {m_renderWidth, m_renderHeight}, m_swapchain.extent);
    }
    m_camera.aspect =
        static_cast<float>(m_swapchain.extent.width) / static_cast<float>(std::max(m_swapchain.extent.height, 1u));
    m_accumulatedFrames = 0;
    m_resetAccumulation = true;
    m_suspended = false;

    RT_LOG("Application resume: %ux%u", m_swapchain.extent.width, m_swapchain.extent.height);
}

void Application::applyInput(const FrameInput& input)
{
    if (input.toggleUI)
    {
        m_settings.showUI = !m_settings.showUI;
    }

    const bool uiCaptured = m_settings.showUI && ImGui::GetIO().WantCaptureMouse;
    if (!uiCaptured)
    {
        if (glm::dot(input.orbitDelta, input.orbitDelta) > 0.0f)
        {
            m_camera.orbit(input.orbitDelta.x, input.orbitDelta.y);
        }
        if (glm::dot(input.panDelta, input.panDelta) > 0.0f)
        {
            m_camera.pan(input.panDelta.x, input.panDelta.y);
        }
        if (std::abs(input.zoomDelta) > 1e-6f)
        {
            m_camera.zoom(input.zoomDelta);
        }
    }

    const glm::vec3 cameraPos = m_camera.position();
    if (glm::length(cameraPos - m_lastCameraPos) > 1e-5f || std::abs(m_camera.fovY - m_lastFov) > 1e-5f)
    {
        m_resetAccumulation = true;
    }
    m_lastCameraPos = cameraPos;
    m_lastFov = m_camera.fovY;
}

void Application::updateConstants()
{
    std::memset(&m_constants, 0, sizeof(FrameConstants));

    const glm::vec3 position = m_camera.position();
    m_constants.camPos = glm::vec4(position, 1.0f);
    m_constants.camRight = glm::vec4(m_camera.right(), 0.0f);
    m_constants.camUp = glm::vec4(m_camera.up(), 0.0f);
    m_constants.camForward = glm::vec4(m_camera.forward(), 0.0f);
    m_constants.resolution = glm::vec2(static_cast<float>(m_renderWidth), static_cast<float>(m_renderHeight));
    m_constants.outputResolution =
        glm::vec2(static_cast<float>(m_swapchain.extent.width), static_cast<float>(m_swapchain.extent.height));
    m_constants.frameIndex = m_accumulatedFrames;
    m_constants.maxBounces = static_cast<uint32_t>(std::max(m_settings.maxBounces, 1));
    m_constants.neeBounces = static_cast<uint32_t>(std::clamp(m_settings.neeBounces, 0, m_settings.maxBounces));
    m_constants.samplesPerFrame = static_cast<uint32_t>(std::max(m_settings.samplesPerFrame, 1));
    m_constants.lightCount = m_gpuScene.lightCount;
    m_constants.flags = (m_swapchain.hdr() ? 1u : 0u) | (m_activeScene == 2 ? 2u : 0u);
    m_constants.fov = m_camera.fovY;

    const float azimuth = glm::radians(m_settings.sunAzimuth);
    const float elevation = glm::radians(m_settings.sunElevation);
    const glm::vec3 sunDirection(std::cos(elevation) * std::sin(azimuth), std::sin(elevation),
                                 std::cos(elevation) * std::cos(azimuth));
    m_constants.sunDir = glm::vec4(glm::normalize(sunDirection), 1.0f);
    if (m_activeScene == 2)
    {
        // 夕阳：低角度暖色太阳
        m_constants.sunColor = glm::vec4(1.0f, 0.52f, 0.24f, m_settings.sunIntensity);
    }
    else if (m_activeScene == 3)
    {
        // 阳光展厅：暖白阳光
        m_constants.sunColor = glm::vec4(1.0f, 0.82f, 0.62f, m_settings.sunIntensity);
    }
    else
    {
        m_constants.sunColor = glm::vec4(1.0f, 0.95f, 0.86f, m_settings.sunIntensity);
    }

    m_constants.exposure = m_settings.exposure;
    m_constants.envIntensity = m_settings.envIntensity;
    m_constants.fireflyClamp = m_settings.fireflyClamp;
    m_constants.paperWhiteNits = 203.0f;
    m_constants.hdrPeakNits = m_settings.hdrPeakNits;
    m_constants.fogDensity = m_settings.fogDensity;
    m_constants.fogAlbedo = m_settings.fogAlbedo;
    m_constants.fogAnisotropy = m_settings.fogAnisotropy;
    m_constants.fogDistance = m_fogDistance;
}

bool Application::drawUI()
{
    bool switchScene = false;
    if (!m_settings.showUI)
    {
        return false;
    }

    const ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(720.0f, 900.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Ray Tracer");

    static const char* scenes[] = {"Procedural (instanced)", "Sponza (glTF)", "HDR Showcase (sunset fog)",
                                   "Sunlit Gallery (light shafts)", nullptr};
    int sceneIndex = m_settings.sceneIndex;
    if (ImGui::Combo("Scene", &sceneIndex, scenes, 4))
    {
        m_settings.sceneIndex = sceneIndex;
        switchScene = true;
    }

    if (m_rayTracer.backendAvailable(RayTracer::Backend::RayTracingPipeline) &&
        m_rayTracer.backendAvailable(RayTracer::Backend::RayQueryCompute))
    {
        static const char* backends[] = {"Ray tracing pipeline", "Ray query compute", nullptr};
        int backendIndex = m_rayTracer.backend() == RayTracer::Backend::RayTracingPipeline ? 0 : 1;
        if (ImGui::Combo("Backend", &backendIndex, backends, 2))
        {
            m_rayTracer.setBackend(backendIndex == 0 ? RayTracer::Backend::RayTracingPipeline
                                                     : RayTracer::Backend::RayQueryCompute);
            m_resetAccumulation = true;
        }
    }

    ImGui::SliderInt("Max bounces", &m_settings.maxBounces, 1, 8);
    ImGui::SliderInt("NEE bounces", &m_settings.neeBounces, 0, 4);
    ImGui::SliderInt("Samples / frame", &m_settings.samplesPerFrame, 1, 4);
    ImGui::SliderFloat("Render scale", &m_settings.renderScale, 0.25f, 1.0f, "%.2f");
    ImGui::SliderFloat("Exposure", &m_settings.exposure, 0.1f, 4.0f, "%.2f");
    ImGui::SliderFloat("Sun intensity", &m_settings.sunIntensity, 0.0f, 15.0f, "%.2f");
    ImGui::SliderFloat("Sun azimuth", &m_settings.sunAzimuth, 0.0f, 360.0f, "%.0f deg");
    ImGui::SliderFloat("Sun elevation", &m_settings.sunElevation, 1.0f, 89.0f, "%.0f deg");
    ImGui::SliderFloat("Environment", &m_settings.envIntensity, 0.0f, 3.0f, "%.2f");
    ImGui::SliderFloat("Firefly clamp", &m_settings.fireflyClamp, 1.0f, 1000.0f, "%.1f");

    ImGui::Separator();
    ImGui::SliderFloat("Fog density", &m_settings.fogDensity, 0.0f, 0.15f, "%.3f");
    ImGui::SliderFloat("Fog albedo", &m_settings.fogAlbedo, 0.1f, 1.0f, "%.2f");
    ImGui::SliderFloat("Fog anisotropy", &m_settings.fogAnisotropy, -0.9f, 0.9f, "%.2f");

    if (m_platform->hdrOutputSupported())
    {
        if (ImGui::Checkbox("HDR output", &m_settings.hdrOutput))
        {
            m_pendingHdrToggle = true;
        }
        if (m_swapchain.hdr())
        {
            ImGui::SliderFloat("HDR peak (nits)", &m_settings.hdrPeakNits, 400.0f, 4000.0f, "%.0f");
        }
        else
        {
            ImGui::TextDisabled("HDR unavailable: SDR output");
        }
    }

    ImGui::Separator();
    float fovDegrees = glm::degrees(m_camera.fovY);
    if (ImGui::SliderFloat("Field of view", &fovDegrees, 30.0f, 100.0f, "%.0f deg"))
    {
        m_camera.fovY = glm::radians(fovDegrees);
    }

    if (ImGui::Button("Reset camera"))
    {
        const Scene& scene = m_scenes[m_activeScene < 0 ? 0 : m_activeScene];
        m_camera.reset(scene.center(), scene.radius());
        m_resetAccumulation = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset accumulation"))
    {
        m_resetAccumulation = true;
    }

    ImGui::Separator();
#if defined(__ANDROID__)
    ImGui::TextUnformatted("One finger: orbit");
    ImGui::TextUnformatted("Two fingers: pan / pinch zoom");
    ImGui::TextUnformatted("Back: toggle UI");
    if (ImGui::Button("Exit"))
    {
        m_platform->requestClose();
    }
#else
    ImGui::TextUnformatted("LMB: orbit  RMB: pan  Wheel: zoom");
    ImGui::TextUnformatted("F1: toggle UI");
#endif
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 460.0f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(440.0f, 380.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Statistics");
    ImGui::Text("FPS: %.1f (%.2f ms)", m_fps, m_frameMs);
    ImGui::Text("Accumulated: %u samples", m_accumulatedFrames);
    ImGui::Text("Render: %ux%u (%.2f)", m_renderWidth, m_renderHeight, m_settings.renderScale);
    ImGui::Text("Output: %ux%u", m_swapchain.extent.width, m_swapchain.extent.height);
    ImGui::Text("Backend: %s", m_rayTracer.backendName());
    ImGui::Text("Instances: %u", m_gpuScene.instanceCount);
    ImGui::Text("Triangles: %u", m_gpuScene.triangleCount);
    ImGui::Text("Lights: %u", m_gpuScene.lightCount);
    ImGui::Text("Fog: %.3f (g %.2f)", m_settings.fogDensity, m_settings.fogAnisotropy);
    if (m_swapchain.hdr())
    {
        ImGui::Text("HDR: on (PQ, %.0f nits peak)", m_settings.hdrPeakNits);
    }
    else
    {
        ImGui::Text("HDR: off");
    }
    ImGui::Text("GPU: %s", m_context.props.deviceName);
    ImGui::End();

    return switchScene;
}

void Application::recordFrame(VkCommandBuffer cmd, uint32_t imageIndex, bool resetAccumulation)
{
    m_rayTracer.render(cmd, m_constants, resetAccumulation, m_swapchain.images[imageIndex],
                       m_swapchain.views[imageIndex],
                       [this](VkCommandBuffer overlayCmd) { m_ui.render(overlayCmd); });

    const VkImage image = m_swapchain.images[imageIndex];
#if !defined(__ANDROID__)
    const bool capture = !m_options.screenshot.empty() && m_options.frames > 0 &&
                         (m_frameCount + 1 >= m_options.frames);
#else
    const bool capture = false;
#endif

    if (capture)
    {
        transitionImage(cmd, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {m_swapchain.extent.width, m_swapchain.extent.height, 1};
        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_readback.buffer, 1, &region);

        transitionImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }
    else
    {
        transitionImage(cmd, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }
}

#if !defined(__ANDROID__)
void Application::captureScreenshot(uint32_t)
{
    const uint32_t width = m_swapchain.extent.width;
    const uint32_t height = m_swapchain.extent.height;
    const size_t pixelCount = static_cast<size_t>(width) * height;

    const auto* source = static_cast<const uint8_t*>(m_readback.mapped);
    std::vector<uint8_t> rgba(pixelCount * 4);

    const bool bgra = m_swapchain.format == VK_FORMAT_B8G8R8A8_UNORM ||
                      m_swapchain.format == VK_FORMAT_B8G8R8A8_SRGB;
    for (size_t i = 0; i < pixelCount; ++i)
    {
        if (bgra)
        {
            rgba[i * 4 + 0] = source[i * 4 + 2];
            rgba[i * 4 + 1] = source[i * 4 + 1];
            rgba[i * 4 + 2] = source[i * 4 + 0];
            rgba[i * 4 + 3] = source[i * 4 + 3];
        }
        else
        {
            std::memcpy(&rgba[i * 4], &source[i * 4], 4);
        }
    }

    if (!savePng(m_options.screenshot, static_cast<int>(width), static_cast<int>(height), rgba.data()))
    {
        RT_LOG("Failed to save screenshot: %s", m_options.screenshot.c_str());
    }
    else
    {
        RT_LOG("Screenshot saved: %s", m_options.screenshot.c_str());
    }
}
#else
void Application::captureScreenshot(uint32_t) {}
#endif

void Application::tick(Platform& platform, const FrameInput& input)
{
    const auto now = std::chrono::steady_clock::now();
    m_frameMs = static_cast<float>(std::chrono::duration<double, std::milli>(now - m_lastTime).count());
    m_fps = m_frameMs > 0.0f ? 1000.0f / m_frameMs : 0.0f;
    m_lastTime = now;

    m_ui.newFrame();
    applyInput(input);

    const Settings before = m_settings;
    const bool switchScene = drawUI();
    if (switchScene)
    {
        updateScene(m_settings.sceneIndex);
    }
    else if (renderSettingsDiffer(before, m_settings))
    {
        m_resetAccumulation = true;
    }

    if (std::abs(before.hdrPeakNits - m_settings.hdrPeakNits) > 0.5f)
    {
        m_swapchain.setHdrMetadata(m_context, m_settings.hdrPeakNits);
    }

    if (std::abs(before.renderScale - m_settings.renderScale) > 1e-6f && m_activeScene >= 0)
    {
        updateRenderExtent();
        m_rayTracer.setScene(m_gpuScene, {m_renderWidth, m_renderHeight}, m_swapchain.extent);
        m_resetAccumulation = true;
    }

    m_camera.aspect =
        static_cast<float>(m_swapchain.extent.width) / static_cast<float>(std::max(m_swapchain.extent.height, 1u));
    updateConstants();

#ifdef RTDEMO_HAVE_RENDERDOC
    if (m_renderDoc && m_options.renderDocCapture && m_frameCount == 3)
    {
        m_renderDoc->StartFrameCapture(nullptr, nullptr);
        m_renderDocCaptureStarted = true;
    }
#endif

    vkWaitForFences(m_context.device, 1, &m_frameFence, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    const VkResult acquire = m_swapchain.acquireNext(m_context, m_imageAvailable, &imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
    {
        recreateSwapchain(true);
        return;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR)
    {
        RT_FATAL("vkAcquireNextImageKHR failed (%d)", static_cast<int>(acquire));
    }

    vkResetFences(m_context.device, 1, &m_frameFence);
    vkResetCommandBuffer(m_commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_commandBuffer, &beginInfo);
    recordFrame(m_commandBuffer, imageIndex, m_resetAccumulation);
    vkEndCommandBuffer(m_commandBuffer);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &m_imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &m_renderFinished[imageIndex];
    vkQueueSubmit(m_context.graphicsQueue, 1, &submitInfo, m_frameFence);

    const VkResult presentResult =
        m_swapchain.present(m_context, m_context.graphicsQueue, m_renderFinished[imageIndex], imageIndex);

    vkWaitForFences(m_context.device, 1, &m_frameFence, VK_TRUE, UINT64_MAX);

#ifdef RTDEMO_HAVE_RENDERDOC
    if (m_renderDocCaptureStarted && m_renderDoc)
    {
        m_renderDoc->EndFrameCapture(nullptr, nullptr);
        m_renderDocCaptureStarted = false;
        RT_LOG("RenderDoc capture written");
    }
#endif

#if !defined(__ANDROID__)
    const bool captureThisFrame = !m_options.screenshot.empty() && m_options.frames > 0 &&
                                  (m_frameCount + 1 >= m_options.frames);
    if (captureThisFrame)
    {
        captureScreenshot(imageIndex);
        platform.requestClose();
    }
    else
#endif
    if (m_options.frames > 0 && m_frameCount + 1 >= m_options.frames)
    {
        platform.requestClose();
    }

    if (m_resetAccumulation)
    {
        m_accumulatedFrames = 0;
        m_resetAccumulation = false;
    }
    ++m_accumulatedFrames;
    ++m_frameCount;

    if (m_frameCount > 0 && (m_frameCount % 120) == 0)
    {
        RT_LOG("FPS %.1f (%.2f ms) scene %d render %ux%u samples %u backend %s", m_fps, m_frameMs, m_activeScene,
               m_renderWidth, m_renderHeight, m_accumulatedFrames, m_rayTracer.backendName());
    }

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        recreateSwapchain(true);
    }
    // Android 合成器可能持续返回 VK_SUBOPTIMAL_KHR，按成功处理，避免每帧重建交换链
}

void Application::run(Platform& platform)
{
    while (!platform.shouldClose())
    {
        FrameInput input{};
        const PlatformEvent event = platform.pumpEvents(input);

        switch (event)
        {
        case PlatformEvent::Quit:
            return;
        case PlatformEvent::SurfaceLost:
            suspend();
            continue;
        case PlatformEvent::SurfaceCreated:
            resume();
            continue;
        default:
            break;
        }

        if (m_pendingHdrToggle)
        {
            applyHdrToggle();
            continue;
        }

        if (!platform.windowValid())
        {
            platform.waitForWindow();
            continue;
        }

        if (event == PlatformEvent::Resized)
        {
            recreateSwapchain();
            if (m_suspended)
            {
                continue;
            }
        }

        tick(platform, input);
    }
}

void Application::shutdown()
{
    if (m_context.device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(m_context.device);
    }

    if (m_context.device != VK_NULL_HANDLE)
    {
        if (m_uiInitialized && m_platform)
        {
            m_platform->setImGuiInputEnabled(false);
            m_ui.shutdown();
            m_uiInitialized = false;
        }
        m_rayTracer.shutdown();
        m_gpuScene.destroy();
        destroyFrameResources();
        m_swapchain.shutdown(m_context);
        m_resources.shutdown();
    }

    m_context.shutdown();
}
