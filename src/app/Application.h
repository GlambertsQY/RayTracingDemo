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

#include <chrono>
#include <string>
#include <vector>

class Platform;
struct FrameInput;

#ifdef RTDEMO_HAVE_RENDERDOC
#include <renderdoc_app.h>
#endif

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
    bool init(Platform& platform, const Options& options);
    void run(Platform& platform);
    void shutdown();

private:
    void tick(Platform& platform, const FrameInput& input);
    void applyInput(const FrameInput& input);
    void createFrameResources();
    void destroyFrameResources();
    void recreateSwapchain(bool force = false);
    void initSwapchain();
    void applyHdrToggle();
    void suspend();
    void resume();
    void updateRenderExtent();
    void updateScene(int sceneIndex);
    void loadSceneIfNeeded(int sceneIndex);
    bool drawUI();
    void updateConstants();
    void recordFrame(VkCommandBuffer cmd, uint32_t imageIndex, bool resetAccumulation);
    void captureScreenshot(uint32_t imageIndex);

    Options m_options;
    Settings m_settings;
    Scene m_scenes[4];
    bool m_sceneLoaded[4]{false, false, false, false};
    int m_activeScene = -1;
    float m_fogDistance = 100.0f;

    Platform* m_platform = nullptr;
    Context m_context;
    Swapchain m_swapchain;
    Resources m_resources;
    GpuScene m_gpuScene;
    RayTracer m_rayTracer;
    ImGuiLayer m_ui;
    Camera m_camera;

    bool m_uiInitialized = false;
    bool m_suspended = false;
    bool m_pendingHdrToggle = false;

    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkFence m_frameFence = VK_NULL_HANDLE;
    VkSemaphore m_imageAvailable = VK_NULL_HANDLE;
    std::vector<VkSemaphore> m_renderFinished;

    Buffer m_readback;

    FrameConstants m_constants{};
    uint32_t m_accumulatedFrames = 0;
    uint32_t m_staticFrames = 0;
    bool m_resetAccumulation = true;
    bool m_sceneChanged = false;

    int m_frameCount = 0;
    std::chrono::steady_clock::time_point m_lastTime{};
    float m_fps = 0.0f;
    float m_frameMs = 0.0f;
    float m_hdrMaxCll = 0.0f;
    float m_hdrMaxFall = 0.0f;

    glm::vec3 m_lastCameraPos{0.0f};
    float m_lastFov = 0.9f;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_renderWidth = 0;
    uint32_t m_renderHeight = 0;

#ifdef RTDEMO_HAVE_RENDERDOC
    RENDERDOC_API_1_7_0* m_renderDoc = nullptr;
    bool m_renderDocCaptureStarted = false;
#endif
};
