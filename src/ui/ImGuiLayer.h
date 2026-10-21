#pragma once

#include <volk.h>

class Context;
class Platform;

class ImGuiLayer
{
public:
    void init(Context& context, Platform& platform, VkFormat colorFormat, uint32_t imageCount);
    void shutdown();

    void newFrame();
    void render(VkCommandBuffer cmd);

    bool wantCaptureMouse() const;
    bool wantCaptureKeyboard() const;

private:
    Context* m_context = nullptr;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
};
