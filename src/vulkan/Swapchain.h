#pragma once

#include <volk.h>
#include <cstdint>
#include <vector>

class Context;

class Swapchain
{
public:
    void init(Context& context, uint32_t width, uint32_t height, bool preferHdr = false);
    void shutdown(Context& context);
    void recreate(Context& context, uint32_t width, uint32_t height, bool preferHdr = false);
    // 更新 HDR 内容峰值亮度（同时刷新交换链的 HDR metadata）
    void setHdrMetadata(Context& context, float maxLuminanceNits);
    // 根据画面统计刷新 MaxCLL / MaxFALL（单位 nits）
    void setHdrContentStats(Context& context, float maxCllNits, float maxFallNits);
    VkResult acquireNext(Context& context, VkSemaphore semaphore, uint32_t* imageIndex);
    VkResult present(Context& context, VkQueue queue, VkSemaphore waitSemaphore, uint32_t imageIndex);

    VkExtent2D extent{0, 0};
    VkFormat   format = VK_FORMAT_B8G8R8A8_UNORM;
    VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    std::vector<VkImage> images;
    std::vector<VkImageView> views;

    uint32_t imageCount() const { return static_cast<uint32_t>(images.size()); }
    VkSwapchainKHR handle() const { return m_swapchain; }
    bool hdr() const { return m_hdr; }   // 实际输出是否为 HDR10 (PQ / BT.2020)

private:
    void create(Context& context, uint32_t width, uint32_t height, bool preferHdr, VkSwapchainKHR old);
    void destroy(Context& context);
    void applyHdrMetadata(Context& context);

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    bool           m_hdr = false;
    float          m_hdrPeakNits = 1000.0f;
    float          m_maxCllNits = 1000.0f;
    float          m_maxFallNits = 400.0f;
};
