#include "app/Application.h"

#include "common/Log.h"
#include "scene/GltfLoader.h"
#include "scene/TextureLoader.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <vector>

#ifdef RTDEMO_HAVE_RENDERDOC
#include <windows.h>
#endif

namespace
{
float g_scrollDelta = 0.0f;

void scrollCallback(GLFWwindow*, double, double yOffset)
{
    g_scrollDelta += static_cast<float>(yOffset);
}

bool renderSettingsDiffer(const Settings& a, const Settings& b)
{
    return a.maxBounces != b.maxBounces || a.samplesPerFrame != b.samplesPerFrame || a.exposure != b.exposure ||
           a.sunIntensity != b.sunIntensity || a.sunAzimuth != b.sunAzimuth || a.sunElevation != b.sunElevation ||
           a.envIntensity != b.envIntensity || a.fireflyClamp != b.fireflyClamp;
}
} // namespace

bool Application::init(const Options& options)
{
    m_options = options;
    m_settings.sceneIndex = options.scene;
    m_settings.showUI = options.screenshot.empty();

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

    if (!glfwInit())
    {
        RT_LOG("glfwInit failed");
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    m_window = glfwCreateWindow(options.width, options.height, "Vulkan Hardware Ray Tracing Demo", nullptr, nullptr);
    if (!m_window)
    {
        RT_LOG("Failed to create window");
        return false;
    }
    glfwSetScrollCallback(m_window, scrollCallback);

    m_context.init(m_window);
    m_resources.init(m_context);

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(m_window, &framebufferWidth, &framebufferHeight);
    m_width = static_cast<uint32_t>(std::max(framebufferWidth, 1));
    m_height = static_cast<uint32_t>(std::max(framebufferHeight, 1));
    m_swapchain.init(m_context, m_window, m_width, m_height);

    createFrameResources();
    m_rayTracer.init(m_context, m_resources, m_swapchain.format);
    m_ui.init(m_context, m_window, m_swapchain.format, m_swapchain.imageCount());

    m_camera.aspect = static_cast<float>(m_swapchain.extent.width) /
                      static_cast<float>(std::max(m_swapchain.extent.height, 1u));
    updateScene(m_settings.sceneIndex);

    m_lastTime = glfwGetTime();
    return true;
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

    const VkDeviceSize readbackSize = static_cast<VkDeviceSize>(m_swapchain.extent.width) *
                                      m_swapchain.extent.height * 4;
    m_readback = m_resources.createBuffer(readbackSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_ONLY,
                                          true);
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
    if (sceneIndex < 0 || sceneIndex > 1 || m_sceneLoaded[sceneIndex])
    {
        return;
    }

    if (sceneIndex == 1)
    {
        const std::string path = std::string(RTDEMO_ASSET_DIR) + "/Sponza/Sponza.gltf";
        std::string error;
        if (std::filesystem::exists(path) && loadGltfScene(path, m_scenes[1], error))
        {
            m_sceneLoaded[1] = true;
            return;
        }

        RT_LOG("Sponza unavailable (%s), falling back to procedural scene",
               error.empty() ? "asset not found" : error.c_str());
    }

    m_scenes[0] = buildProceduralScene();
    m_sceneLoaded[0] = true;
    if (sceneIndex == 1)
    {
        m_settings.sceneIndex = 0;
    }
}

void Application::updateScene(int sceneIndex)
{
    loadSceneIfNeeded(sceneIndex);

    const int actualScene = m_settings.sceneIndex;
    const Scene& scene = m_scenes[actualScene];

    m_gpuScene.destroy();
    m_gpuScene.create(m_context, m_resources, scene);
    m_rayTracer.setScene(m_gpuScene, m_swapchain.extent);

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
    m_camera.aspect = static_cast<float>(m_swapchain.extent.width) /
                      static_cast<float>(std::max(m_swapchain.extent.height, 1u));
    m_lastCameraPos = m_camera.position();
    m_lastFov = m_camera.fovY;

    m_activeScene = actualScene;
    m_accumulatedFrames = 0;
    m_resetAccumulation = true;
}

void Application::recreateSwapchain()
{
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(m_window, &framebufferWidth, &framebufferHeight);
    if (framebufferWidth <= 0 || framebufferHeight <= 0)
    {
        return;
    }

    vkDeviceWaitIdle(m_context.device);
    m_swapchain.recreate(m_context, m_window, static_cast<uint32_t>(framebufferWidth),
                         static_cast<uint32_t>(framebufferHeight));

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

    m_resources.destroyBuffer(m_readback);
    const VkDeviceSize readbackSize =
        static_cast<VkDeviceSize>(m_swapchain.extent.width) * m_swapchain.extent.height * 4;
    m_readback = m_resources.createBuffer(readbackSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_ONLY,
                                          true);

    m_rayTracer.setScene(m_gpuScene, m_swapchain.extent);
    m_camera.aspect = static_cast<float>(m_swapchain.extent.width) /
                      static_cast<float>(std::max(m_swapchain.extent.height, 1u));
    m_accumulatedFrames = 0;
    m_resetAccumulation = true;
}

void Application::processInput()
{
    ImGuiIO& io = ImGui::GetIO();

    static bool f1Pressed = false;
    const bool f1Down = glfwGetKey(m_window, GLFW_KEY_F1) == GLFW_PRESS;
    if (f1Down && !f1Pressed)
    {
        m_settings.showUI = !m_settings.showUI;
    }
    f1Pressed = f1Down;

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(m_window, &mouseX, &mouseY);

    static double lastX = 0.0;
    static double lastY = 0.0;
    static bool orbiting = false;
    static bool panning = false;

    const bool uiCaptured = m_settings.showUI && io.WantCaptureMouse;
    const float dx = static_cast<float>(mouseX - lastX);
    const float dy = static_cast<float>(mouseY - lastY);

    if (!uiCaptured)
    {
        const bool leftDown = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        const bool rightDown = glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

        if (leftDown)
        {
            if (orbiting)
            {
                m_camera.orbit(dx, dy);
            }
            orbiting = true;
        }
        else
        {
            orbiting = false;
        }

        if (rightDown)
        {
            if (panning)
            {
                m_camera.pan(dx, dy);
            }
            panning = true;
        }
        else
        {
            panning = false;
        }

        if (g_scrollDelta != 0.0f)
        {
            m_camera.zoom(g_scrollDelta);
        }
    }

    lastX = mouseX;
    lastY = mouseY;
    g_scrollDelta = 0.0f;

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
    m_constants.resolution = glm::vec2(m_swapchain.extent.width, m_swapchain.extent.height);
    m_constants.frameIndex = m_accumulatedFrames;
    m_constants.maxBounces = static_cast<uint32_t>(std::max(m_settings.maxBounces, 1));
    m_constants.samplesPerFrame = static_cast<uint32_t>(std::max(m_settings.samplesPerFrame, 1));
    m_constants.lightCount = m_gpuScene.lightCount;
    m_constants.flags = 0;
    m_constants.fov = m_camera.fovY;

    const float azimuth = glm::radians(m_settings.sunAzimuth);
    const float elevation = glm::radians(m_settings.sunElevation);
    const glm::vec3 sunDirection(std::cos(elevation) * std::sin(azimuth), std::sin(elevation),
                                 std::cos(elevation) * std::cos(azimuth));
    m_constants.sunDir = glm::vec4(glm::normalize(sunDirection), 1.0f);
    m_constants.sunColor = glm::vec4(1.0f, 0.95f, 0.86f, m_settings.sunIntensity);

    m_constants.exposure = m_settings.exposure;
    m_constants.envIntensity = m_settings.envIntensity;
    m_constants.fireflyClamp = m_settings.fireflyClamp;
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
    ImGui::SetNextWindowSize(ImVec2(390.0f, 470.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Ray Tracer");

    static const char* scenes[] = {"Procedural (instanced)", "Sponza (glTF)", nullptr};
    int sceneIndex = m_settings.sceneIndex;
    if (ImGui::Combo("Scene", &sceneIndex, scenes, 2))
    {
        m_settings.sceneIndex = sceneIndex;
        switchScene = true;
    }

    ImGui::SliderInt("Max bounces", &m_settings.maxBounces, 1, 8);
    ImGui::SliderInt("Samples / frame", &m_settings.samplesPerFrame, 1, 4);
    ImGui::SliderFloat("Exposure", &m_settings.exposure, 0.1f, 4.0f, "%.2f");
    ImGui::SliderFloat("Sun intensity", &m_settings.sunIntensity, 0.0f, 15.0f, "%.2f");
    ImGui::SliderFloat("Sun azimuth", &m_settings.sunAzimuth, 0.0f, 360.0f, "%.0f deg");
    ImGui::SliderFloat("Sun elevation", &m_settings.sunElevation, 1.0f, 89.0f, "%.0f deg");
    ImGui::SliderFloat("Environment", &m_settings.envIntensity, 0.0f, 3.0f, "%.2f");
    ImGui::SliderFloat("Firefly clamp", &m_settings.fireflyClamp, 1.0f, 50.0f, "%.1f");

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
    ImGui::TextUnformatted("LMB: orbit  RMB: pan  Wheel: zoom");
    ImGui::TextUnformatted("F1: toggle UI");
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 360.0f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(344.0f, 200.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Statistics");
    ImGui::Text("FPS: %.1f (%.2f ms)", m_fps, m_frameMs);
    ImGui::Text("Accumulated: %u samples", m_accumulatedFrames);
    ImGui::Text("Resolution: %ux%u", m_swapchain.extent.width, m_swapchain.extent.height);
    ImGui::Text("Instances: %u", m_gpuScene.instanceCount);
    ImGui::Text("Triangles: %u", m_gpuScene.triangleCount);
    ImGui::Text("Lights: %u", m_gpuScene.lightCount);
    ImGui::Text("GPU: %s", m_context.props.deviceName);
    ImGui::End();

    return switchScene;
}

void Application::recordFrame(VkCommandBuffer cmd, uint32_t imageIndex, bool resetAccumulation)
{
    const bool capture = !m_options.screenshot.empty() && m_options.frames > 0 &&
                         (m_frameCount + 1 >= m_options.frames);

    m_rayTracer.render(cmd, m_constants, resetAccumulation, m_swapchain.images[imageIndex],
                       m_swapchain.views[imageIndex],
                       [this](VkCommandBuffer overlayCmd) { m_ui.render(overlayCmd); });

    const VkImage image = m_swapchain.images[imageIndex];
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

void Application::run()
{
    while (!glfwWindowShouldClose(m_window))
    {
        glfwPollEvents();

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(m_window, &framebufferWidth, &framebufferHeight);
        if (framebufferWidth <= 0 || framebufferHeight <= 0)
        {
            glfwWaitEvents();
            continue;
        }

        if (static_cast<uint32_t>(framebufferWidth) != m_swapchain.extent.width ||
            static_cast<uint32_t>(framebufferHeight) != m_swapchain.extent.height)
        {
            recreateSwapchain();
            continue;
        }

        const double now = glfwGetTime();
        m_frameMs = static_cast<float>((now - m_lastTime) * 1000.0);
        m_fps = m_frameMs > 0.0f ? 1000.0f / m_frameMs : 0.0f;
        m_lastTime = now;

        m_ui.newFrame();
        processInput();

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

        m_camera.aspect = static_cast<float>(m_swapchain.extent.width) /
                          static_cast<float>(std::max(m_swapchain.extent.height, 1u));
        updateConstants();

        const bool captureThisFrame = !m_options.screenshot.empty() && m_options.frames > 0 &&
                                      (m_frameCount + 1 >= m_options.frames);

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
            recreateSwapchain();
            continue;
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

        if (captureThisFrame)
        {
            captureScreenshot(imageIndex);
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        }
        else if (m_options.frames > 0 && m_frameCount + 1 >= m_options.frames)
        {
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        }

        if (m_resetAccumulation)
        {
            m_accumulatedFrames = 0;
            m_resetAccumulation = false;
        }
        ++m_accumulatedFrames;
        ++m_frameCount;

        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
        {
            recreateSwapchain();
        }
    }
}

void Application::shutdown()
{
    if (m_context.device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(m_context.device);
    }

    if (m_window && m_context.device != VK_NULL_HANDLE)
    {
        m_ui.shutdown();
        m_rayTracer.shutdown();
        m_gpuScene.destroy();
        destroyFrameResources();
        m_swapchain.shutdown(m_context);
        m_resources.shutdown();
        m_context.shutdown();
    }

    if (m_window)
    {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwTerminate();
}
