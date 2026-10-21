#pragma once

#include <volk.h>

class Context;
struct GLFWwindow;

class ImGuiLayer
{
public:
    void init(Context& context, GLFWwindow* window, VkFormat colorFormat, uint32_t imageCount);
    void shutdown();

    void newFrame();
    void render(VkCommandBuffer cmd);

    bool wantCaptureMouse() const;
    bool wantCaptureKeyboard() const;

private:
    Context* m_context = nullptr;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
};
