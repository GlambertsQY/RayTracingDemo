#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <functional>

class Context;

struct Buffer
{
    VkBuffer        buffer = VK_NULL_HANDLE;
    VmaAllocation   allocation = VK_NULL_HANDLE;
    VkDeviceAddress address = 0;
    VkDeviceSize    size = 0;
    void*           mapped = nullptr;
};

struct Image
{
    VkImage       image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView   view = VK_NULL_HANDLE;
    VkFormat      format = VK_FORMAT_UNDEFINED;
    VkExtent2D    extent{0, 0};
    uint32_t      mipLevels = 1;
};

// 颜色附件初始布局转换（供各 Pass 复用）
void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                     VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT, uint32_t mipLevels = 1,
                     uint32_t baseMip = 0);

class Resources
{
public:
    void init(Context& context);
    void shutdown();

    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memory, bool map = false);
    void destroyBuffer(Buffer& buffer);
    void uploadBuffer(Buffer& buffer, const void* data, VkDeviceSize size);

    Image createImage2D(VkFormat format, VkExtent2D extent, VkImageUsageFlags usage, uint32_t mipLevels = 1);
    Image createTextureImage(VkFormat format, VkExtent2D extent, const void* pixels, VkDeviceSize size,
                             bool generateMips);
    void destroyImage(Image& image);

    VkSampler createSampler(uint32_t mipLevels) const;

    void immediate(const std::function<void(VkCommandBuffer)>& record);

    Context* context = nullptr;
    VkCommandPool commandPool = VK_NULL_HANDLE;
};
