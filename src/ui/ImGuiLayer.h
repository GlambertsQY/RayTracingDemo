#pragma once

#include <volk.h>

#include <cstdint>
#include <vector>

class Context;
class Platform;

class ImGuiLayer
{
public:
    void init(Context& context, Platform& platform, VkFormat colorFormat, uint32_t imageCount, bool hdrOutput = false);
    void shutdown();

    void newFrame();
    void render(VkCommandBuffer cmd);

    bool wantCaptureMouse() const;
    bool wantCaptureKeyboard() const;

private:
    Context* m_context = nullptr;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    std::vector<uint8_t> m_hdrFragSpirv;   // HDR 输出时使用的自定义 fragment shader
};
