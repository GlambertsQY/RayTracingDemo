#include "platform/Platform.h"

#include "common/Log.h"

#include <GLFW/glfw3.h>

#include <algorithm>

namespace
{
float g_scrollDelta = 0.0f;

void scrollCallback(GLFWwindow*, double, double yOffset)
{
    g_scrollDelta += static_cast<float>(yOffset);
}

class PlatformWindows final : public Platform
{
public:
    bool init(const char* title, int width, int height) override
    {
        if (!glfwInit())
        {
            RT_LOG("glfwInit failed");
            return false;
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
        if (!m_window)
        {
            RT_LOG("Failed to create window");
            glfwTerminate();
            return false;
        }

        glfwSetScrollCallback(m_window, scrollCallback);
        glfwGetCursorPos(m_window, &m_cursorX, &m_cursorY);
        getFramebufferSize(m_width, m_height);

        uint32_t extensionCount = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
        if (extensions)
        {
            m_extensions.assign(extensions, extensions + extensionCount);
        }
        return true;
    }

    void shutdown() override
    {
        if (m_window)
        {
            glfwDestroyWindow(m_window);
            m_window = nullptr;
        }
        glfwTerminate();
    }

    PlatformEvent pumpEvents(FrameInput& input) override
    {
        glfwPollEvents();

        PlatformEvent event = PlatformEvent::None;

        m_f1Down = glfwGetKey(m_window, GLFW_KEY_F1) == GLFW_PRESS;
        if (m_f1Down && !m_f1Pressed)
        {
            input.toggleUI = true;
        }
        m_f1Pressed = m_f1Down;

        double mouseX = 0.0;
        double mouseY = 0.0;
        glfwGetCursorPos(m_window, &mouseX, &mouseY);

        const float dx = static_cast<float>(mouseX - m_cursorX);
        const float dy = static_cast<float>(mouseY - m_cursorY);
        m_cursorX = mouseX;
        m_cursorY = mouseY;

        if (glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS)
        {
            input.orbitDelta += glm::vec2(dx, dy);
        }
        if (glfwGetMouseButton(m_window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
        {
            input.panDelta += glm::vec2(dx, dy);
        }
        if (g_scrollDelta != 0.0f)
        {
            input.zoomDelta = g_scrollDelta;
            g_scrollDelta = 0.0f;
        }

        uint32_t width = 0;
        uint32_t height = 0;
        getFramebufferSize(width, height);
        if (width != m_width || height != m_height)
        {
            m_width = width;
            m_height = height;
            event = PlatformEvent::Resized;
        }

        if (glfwWindowShouldClose(m_window))
        {
            event = PlatformEvent::Quit;
        }
        return event;
    }

    void waitForWindow() override
    {
        glfwWaitEvents();
    }

    bool windowValid() const override
    {
        if (!m_window)
        {
            return false;
        }
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(m_window, &width, &height);
        return width > 0 && height > 0;
    }

    void getFramebufferSize(uint32_t& width, uint32_t& height) const override
    {
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(m_window, &framebufferWidth, &framebufferHeight);
        width = static_cast<uint32_t>(std::max(framebufferWidth, 1));
        height = static_cast<uint32_t>(std::max(framebufferHeight, 1));
    }

    const std::vector<const char*>& instanceExtensions() const override
    {
        return m_extensions;
    }

    VkSurfaceKHR createSurface(VkInstance instance) override
    {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (glfwCreateWindowSurface(instance, m_window, nullptr, &surface) != VK_SUCCESS)
        {
            RT_LOG("Failed to create window surface");
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

    bool shouldClose() const override
    {
        return !m_window || glfwWindowShouldClose(m_window);
    }

    void requestClose() override
    {
        if (m_window)
        {
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        }
    }

    float uiScale() const override
    {
        return 1.0f;
    }

    void* nativeWindow() override
    {
        return m_window;
    }

    void setImGuiInputEnabled(bool) override {}

private:
    GLFWwindow* m_window = nullptr;
    std::vector<const char*> m_extensions;
    double m_cursorX = 0.0;
    double m_cursorY = 0.0;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_f1Pressed = false;
    bool m_f1Down = false;
};
} // namespace

std::unique_ptr<Platform> createPlatform()
{
    return std::make_unique<PlatformWindows>();
}
