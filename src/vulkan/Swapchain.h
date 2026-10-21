#pragma once

#include <volk.h>
#include <cstdint>
#include <vector>

class Context;

class Swapchain
{
public:
    void init(Context& context, uint32_t width, uint32_t height);
    void shutdown(Context& context);
    void recreate(Context& context, uint32_t width, uint32_t height);
    VkResult acquireNext(Context& context, VkSemaphore semaphore, uint32_t* imageIndex);
    VkResult present(Context& context, VkQueue queue, VkSemaphore waitSemaphore, uint32_t imageIndex);

    VkExtent2D extent{0, 0};
    VkFormat   format = VK_FORMAT_B8G8R8A8_UNORM;
    std::vector<VkImage> images;
    std::vector<VkImageView> views;

    uint32_t imageCount() const { return static_cast<uint32_t>(images.size()); }
    VkSwapchainKHR handle() const { return m_swapchain; }

private:
    void create(Context& context, uint32_t width, uint32_t height, VkSwapchainKHR old);
    void destroy(Context& context);

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
};
