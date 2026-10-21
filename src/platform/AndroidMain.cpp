#if defined(__ANDROID__)

#include "app/Application.h"
#include "common/Assets.h"
#include "common/Log.h"
#include "platform/Platform.h"

#include <android/configuration.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include <imgui_impl_android.h>

#include <algorithm>
#include <cmath>

namespace
{
class PlatformAndroid final : public Platform
{
public:
    explicit PlatformAndroid(android_app* app) : m_app(app)
    {
        m_app->userData = this;
        m_app->onAppCmd = &PlatformAndroid::handleAppCmd;
        m_app->onInputEvent = &PlatformAndroid::handleInputEvent;

        assets::setAssetManager(app->activity->assetManager);

        const int32_t density = AConfiguration_getDensity(app->config);
        m_density = density > 0 ? static_cast<float>(density) / 160.0f : 1.0f;
        m_density = std::clamp(m_density, 1.0f, 4.0f);

        m_extensions = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    }

    bool init(const char*, int, int) override { return true; }

    void shutdown() override {}

    PlatformEvent pumpEvents(FrameInput& input) override
    {
        int events = 0;
        android_poll_source* source = nullptr;
        while (true)
        {
            const int timeout = windowValid() ? 0 : -1;
            const int result = ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void**>(&source));
            if (result < 0)
            {
                break;
            }
            if (source)
            {
                source->process(m_app, source);
            }
            if (m_app->destroyRequested)
            {
                m_quit = true;
                break;
            }
            if (m_pendingEvent != PlatformEvent::None)
            {
                break;
            }
        }

        input.orbitDelta += m_orbitDelta;
        input.panDelta += m_panDelta;
        input.zoomDelta += m_zoomDelta;
        input.toggleUI = input.toggleUI || m_toggleUI;
        m_orbitDelta = glm::vec2(0.0f);
        m_panDelta = glm::vec2(0.0f);
        m_zoomDelta = 0.0f;
        m_toggleUI = false;

        const PlatformEvent event = m_pendingEvent;
        m_pendingEvent = PlatformEvent::None;

        if (m_quit || m_app->destroyRequested)
        {
            return PlatformEvent::Quit;
        }
        return event;
    }

    void waitForWindow() override
    {
        int events = 0;
        android_poll_source* source = nullptr;
        while (!m_app->window && !m_app->destroyRequested)
        {
            if (ALooper_pollOnce(-1, nullptr, &events, reinterpret_cast<void**>(&source)) < 0)
            {
                continue;
            }
            if (source)
            {
                source->process(m_app, source);
            }
        }
    }

    bool windowValid() const override { return m_app->window != nullptr; }

    void getFramebufferSize(uint32_t& width, uint32_t& height) const override
    {
        width = 1;
        height = 1;
        if (m_app->window)
        {
            width = static_cast<uint32_t>(std::max(ANativeWindow_getWidth(m_app->window), 1));
            height = static_cast<uint32_t>(std::max(ANativeWindow_getHeight(m_app->window), 1));
        }
    }

    const std::vector<const char*>& instanceExtensions() const override { return m_extensions; }

    VkSurfaceKHR createSurface(VkInstance instance) override
    {
        if (!m_app->window)
        {
            return VK_NULL_HANDLE;
        }

        VkAndroidSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
        info.window = m_app->window;

        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (vkCreateAndroidSurfaceKHR(instance, &info, nullptr, &surface) != VK_SUCCESS)
        {
            RT_LOG("vkCreateAndroidSurfaceKHR failed");
            return VK_NULL_HANDLE;
        }
        return surface;
    }

    void destroySurface(VkInstance instance, VkSurfaceKHR surface) override
    {
        if (surface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(instance, surface, nullptr);
        }
    }

    bool shouldClose() const override { return m_quit || m_app->destroyRequested; }

    void requestClose() override { m_quit = true; }

    float uiScale() const override { return m_density; }

    void* nativeWindow() override { return m_app->window; }

    void setImGuiInputEnabled(bool enabled) override { m_imguiInputEnabled = enabled; }

private:
    static void handleAppCmd(android_app* app, int32_t cmd)
    {
        auto* platform = static_cast<PlatformAndroid*>(app->userData);
        if (platform)
        {
            platform->onAppCmd(cmd);
        }
    }

    static int32_t handleInputEvent(android_app* app, AInputEvent* event)
    {
        auto* platform = static_cast<PlatformAndroid*>(app->userData);
        return platform ? platform->onInputEvent(event) : 0;
    }

    void onAppCmd(int32_t cmd)
    {
        switch (cmd)
        {
        case APP_CMD_INIT_WINDOW:
            if (m_app->window)
            {
                m_pendingEvent = PlatformEvent::SurfaceCreated;
                m_lastWidth = ANativeWindow_getWidth(m_app->window);
                m_lastHeight = ANativeWindow_getHeight(m_app->window);
                RT_LOG("APP_CMD_INIT_WINDOW %dx%d", m_lastWidth, m_lastHeight);
            }
            break;
        case APP_CMD_TERM_WINDOW:
            RT_LOG("APP_CMD_TERM_WINDOW");
            m_pendingEvent = PlatformEvent::SurfaceLost;
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
            if (m_app->window)
            {
                const int32_t width = ANativeWindow_getWidth(m_app->window);
                const int32_t height = ANativeWindow_getHeight(m_app->window);
                if (width != m_lastWidth || height != m_lastHeight)
                {
                    m_lastWidth = width;
                    m_lastHeight = height;
                    m_pendingEvent = PlatformEvent::Resized;
                }
            }
            break;
        case APP_CMD_GAINED_FOCUS:
            RT_LOG("APP_CMD_GAINED_FOCUS");
            break;
        case APP_CMD_LOST_FOCUS:
            RT_LOG("APP_CMD_LOST_FOCUS");
            break;
        case APP_CMD_LOW_MEMORY:
            RT_LOG("APP_CMD_LOW_MEMORY");
            break;
        case APP_CMD_DESTROY:
            RT_LOG("APP_CMD_DESTROY");
            m_quit = true;
            break;
        default:
            break;
        }
    }

    int32_t onInputEvent(AInputEvent* event)
    {
        const int32_t type = AInputEvent_getType(event);

        if (m_imguiInputEnabled)
        {
            ImGui_ImplAndroid_HandleInputEvent(event);
        }

        if (type == AINPUT_EVENT_TYPE_KEY)
        {
            const int32_t action = AKeyEvent_getAction(event);
            const int32_t keyCode = AKeyEvent_getKeyCode(event);
            if (keyCode == AKEYCODE_BACK && action == AKEY_EVENT_ACTION_DOWN)
            {
                m_toggleUI = true;
                return 1;
            }
            return 0;
        }

        if (type != AINPUT_EVENT_TYPE_MOTION)
        {
            return 0;
        }

        const int32_t action = AMotionEvent_getAction(event);
        const int32_t maskedAction = action & AMOTION_EVENT_ACTION_MASK;
        const int32_t pointerIndex =
            (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        const size_t pointerCount = AMotionEvent_getPointerCount(event);
        const float scale = 1.0f / m_density;

        switch (maskedAction)
        {
        case AMOTION_EVENT_ACTION_DOWN:
            m_activePointers = 1;
            m_lastPoint = {AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0)};
            m_lastMidpoint = m_lastPoint;
            m_lastDistance = 0.0f;
            break;
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            m_activePointers = static_cast<int>(pointerCount);
            if (pointerCount >= 2)
            {
                const glm::vec2 p0(AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0));
                const glm::vec2 p1(AMotionEvent_getX(event, 1), AMotionEvent_getY(event, 1));
                m_lastMidpoint = (p0 + p1) * 0.5f;
                m_lastDistance = glm::length(p1 - p0);
            }
            break;
        case AMOTION_EVENT_ACTION_MOVE:
            if (pointerCount >= 2)
            {
                const glm::vec2 p0(AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0));
                const glm::vec2 p1(AMotionEvent_getX(event, 1), AMotionEvent_getY(event, 1));
                const glm::vec2 midpoint = (p0 + p1) * 0.5f;
                const float distance = glm::length(p1 - p0);

                m_panDelta += (midpoint - m_lastMidpoint) * scale;
                if (m_lastDistance > 1.0f && distance > 1.0f)
                {
                    m_zoomDelta += std::log(m_lastDistance / distance) / 0.12f;
                }
                m_lastMidpoint = midpoint;
                m_lastDistance = distance;
            }
            else if (pointerCount == 1)
            {
                const glm::vec2 point(AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0));
                m_orbitDelta += (point - m_lastPoint) * scale;
                m_lastPoint = point;
            }
            break;
        case AMOTION_EVENT_ACTION_POINTER_UP:
            m_activePointers = static_cast<int>(pointerCount) - 1;
            if (m_activePointers == 1)
            {
                const size_t remaining = pointerIndex == 0 ? 1 : 0;
                m_lastPoint = {AMotionEvent_getX(event, remaining), AMotionEvent_getY(event, remaining)};
                m_lastDistance = 0.0f;
            }
            break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_CANCEL:
            m_activePointers = 0;
            m_lastDistance = 0.0f;
            break;
        default:
            break;
        }

        return 1;
    }

    android_app* m_app = nullptr;
    std::vector<const char*> m_extensions;
    float m_density = 1.0f;
    bool m_quit = false;
    bool m_imguiInputEnabled = false;
    PlatformEvent m_pendingEvent = PlatformEvent::None;
    int32_t m_lastWidth = 0;
    int32_t m_lastHeight = 0;

    int m_activePointers = 0;
    glm::vec2 m_lastPoint{0.0f};
    glm::vec2 m_lastMidpoint{0.0f};
    float m_lastDistance = 0.0f;

    glm::vec2 m_orbitDelta{0.0f};
    glm::vec2 m_panDelta{0.0f};
    float m_zoomDelta = 0.0f;
    bool m_toggleUI = false;
};
} // namespace

void android_main(android_app* state)
{
    PlatformAndroid platform(state);
    FrameInput input{};

    while (!platform.windowValid() && !platform.shouldClose())
    {
        platform.pumpEvents(input);
    }

    if (platform.shouldClose())
    {
        return;
    }

    Application app;
    if (!app.init(platform, Options{}))
    {
        RT_LOG("Application initialization failed");
        app.shutdown();
        ANativeActivity_finish(state->activity);
        return;
    }

    app.run(platform);
    app.shutdown();
    ANativeActivity_finish(state->activity);
}

#endif // __ANDROID__
