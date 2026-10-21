#pragma once

#include "app/Camera.h"
#include "render/GpuScene.h"
#include "render/RayTracer.h"
#include "render/RenderSettings.h"
#include "scene/Scene.h"
#include "ui/ImGuiLayer.h"
#include "vulkan/Context.h"
#include "vulkan/Resources.h"
#include "vulkan/Swapchain.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

#ifdef RTDEMO_HAVE_RENDERDOC
#include <renderdoc_app.h>
#endif

struct GLFWwindow;

struct Options
{
    int width = 1600;
    int height = 900;
    int scene = 0;              // 0 = 程序化, 1 = Sponza
    int frames = -1;            // 渲染 N 帧后退出（用于验证）
    std::string screenshot;     // 非空时保存 PNG
    bool renderDocCapture = false;
};

class Application
{
public:
    bool init(const Options& options);
    void run();
    void shutdown();

private:
    void createFrameResources();
    void destroyFrameResources();
    void recreateSwapchain();
    void updateScene(int sceneIndex);
    void loadSceneIfNeeded(int sceneIndex);
    bool drawUI();
    void processInput();
    void updateConstants();
    void recordFrame(VkCommandBuffer cmd, uint32_t imageIndex, bool resetAccumulation);
    void captureScreenshot(uint32_t imageIndex);

    Options m_options;
    Settings m_settings;
    Scene m_scenes[2];
    bool m_sceneLoaded[2]{false, false};
    int m_activeScene = -1;

    GLFWwindow* m_window = nullptr;
    Context m_context;
    Swapchain m_swapchain;
    Resources m_resources;
    GpuScene m_gpuScene;
    RayTracer m_rayTracer;
    ImGuiLayer m_ui;
    Camera m_camera;

    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkFence m_frameFence = VK_NULL_HANDLE;
    VkSemaphore m_imageAvailable = VK_NULL_HANDLE;
    std::vector<VkSemaphore> m_renderFinished;

    Buffer m_readback;

    FrameConstants m_constants{};
    uint32_t m_accumulatedFrames = 0;
    bool m_resetAccumulation = true;
    bool m_sceneChanged = false;
    bool m_framebufferResized = false;

    int m_frameCount = 0;
    double m_lastTime = 0.0;
    float m_fps = 0.0f;
    float m_frameMs = 0.0f;

    glm::vec3 m_lastCameraPos{0.0f};
    float m_lastFov = 0.9f;

    uint32_t m_width = 0;
    uint32_t m_height = 0;

#ifdef RTDEMO_HAVE_RENDERDOC
    RENDERDOC_API_1_7_0* m_renderDoc = nullptr;
    bool m_renderDocCaptureStarted = false;
#endif
};
