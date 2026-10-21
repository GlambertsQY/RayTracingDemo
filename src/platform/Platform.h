#pragma once

#include <volk.h>

#include <glm/glm.hpp>

#include <memory>
#include <vector>

enum class PlatformEvent
{
    None,
    SurfaceCreated,
    SurfaceLost,
    Resized,
    Quit
};

struct FrameInput
{
    glm::vec2 orbitDelta{0.0f};   // 鼠标左键 / 单指拖动
    glm::vec2 panDelta{0.0f};     // 鼠标右键 / 双指拖动
    float     zoomDelta = 0.0f;   // 滚轮 / 双指捏合
    bool      toggleUI = false;   // F1 / Android 返回键
};

class Platform
{
public:
    virtual ~Platform() = default;

    virtual bool init(const char* title, int width, int height) = 0;
    virtual void shutdown() = 0;

    // 每帧调用；Android 在此处理 APP_CMD_* 与输入事件
    virtual PlatformEvent pumpEvents(FrameInput& input) = 0;
    virtual void waitForWindow() = 0;
    virtual bool windowValid() const = 0;
    virtual void getFramebufferSize(uint32_t& width, uint32_t& height) const = 0;

    virtual const std::vector<const char*>& instanceExtensions() const = 0;
    virtual VkSurfaceKHR createSurface(VkInstance instance) = 0;
    virtual void destroySurface(VkInstance instance, VkSurfaceKHR surface) = 0;

    virtual bool shouldClose() const = 0;
    virtual void requestClose() = 0;
    virtual float uiScale() const = 0;
    virtual void* nativeWindow() = 0;
    virtual void setImGuiInputEnabled(bool enabled) = 0;
};

// Windows 平台工厂（Android 由 android_main 直接构造）
std::unique_ptr<Platform> createPlatform();
