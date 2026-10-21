#include "vulkan/Swapchain.h"

#include "common/Log.h"
#include "vulkan/Context.h"

#include <algorithm>
#include <limits>

void Swapchain::init(Context& context, uint32_t width, uint32_t height, bool preferHdr)
{
    create(context, width, height, preferHdr, VK_NULL_HANDLE);
}

void Swapchain::shutdown(Context& context)
{
    destroy(context);
}

void Swapchain::destroy(Context& context)
{
    for (VkImageView view : views)
    {
        vkDestroyImageView(context.device, view, nullptr);
    }
    views.clear();
    images.clear();
    m_hdr = false;

    if (m_swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(context.device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

void Swapchain::create(Context& context, uint32_t width, uint32_t height, bool preferHdr, VkSwapchainKHR old)
{
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context.physicalDevice, context.surface, &caps);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(context.physicalDevice, context.surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(context.physicalDevice, context.surface, &formatCount, formats.data());

#if defined(__ANDROID__)
    const VkFormat preferredFormat = VK_FORMAT_R8G8B8A8_UNORM;
#else
    const VkFormat preferredFormat = VK_FORMAT_B8G8R8A8_UNORM;
#endif

    VkSurfaceFormatKHR surfaceFormat = formats.empty()
                                           ? VkSurfaceFormatKHR{preferredFormat, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
                                           : formats[0];
    bool hdrSelected = false;
    if (preferHdr && context.swapchainColorSpaceSupported)
    {
        for (const auto& candidate : formats)
        {
            if (candidate.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 &&
                candidate.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT)
            {
                surfaceFormat = candidate;
                hdrSelected = true;
                break;
            }
        }
    }

    if (!hdrSelected)
    {
        for (const auto& candidate : formats)
        {
            if (candidate.format == preferredFormat && candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                surfaceFormat = candidate;
                break;
            }
        }
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(context.physicalDevice, context.surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(context.physicalDevice, context.surface, &presentModeCount,
                                              presentModes.data());

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (VkPresentModeKHR mode : presentModes)
    {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            presentMode = mode;
            break;
        }
    }

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == std::numeric_limits<uint32_t>::max())
    {
        extent.width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    if (extent.width == 0 || extent.height == 0)
    {
        RT_LOG("Swapchain has zero extent");
        return;
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
    {
        imageCount = caps.maxImageCount;
    }

    // 不实现预旋转，交给 Android 合成器处理；否则横屏画面会被旋转 90°
    const VkSurfaceTransformFlagBitsKHR preTransform =
        (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                                                                         : caps.currentTransform;

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = context.surface;
    info.minImageCount = imageCount;
    info.imageFormat = surfaceFormat.format;
    info.imageColorSpace = surfaceFormat.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = preTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = presentMode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = old;

    if (vkCreateSwapchainKHR(context.device, &info, nullptr, &m_swapchain) != VK_SUCCESS)
    {
        RT_FATAL("vkCreateSwapchainKHR failed");
    }

    if (old != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(context.device, old, nullptr);
    }

    format = surfaceFormat.format;
    colorSpace = surfaceFormat.colorSpace;
    m_hdr = hdrSelected;
    this->extent = extent;

    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(context.device, m_swapchain, &actualCount, nullptr);
    images.resize(actualCount);
    vkGetSwapchainImagesKHR(context.device, m_swapchain, &actualCount, images.data());

    views.resize(images.size());
    for (size_t i = 0; i < images.size(); ++i)
    {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(context.device, &viewInfo, nullptr, &views[i]) != VK_SUCCESS)
        {
            RT_FATAL("Failed to create swapchain image view");
        }
    }

    RT_LOG("Swapchain: %ux%u, %zu images, format %d, colorSpace %d (%s)", extent.width, extent.height,
           images.size(), static_cast<int>(format), static_cast<int>(colorSpace), m_hdr ? "HDR10" : "SDR");

    // HDR10 母版/内容亮度信息（设备不支持该扩展时跳过，Android 依赖窗口 dataspace 即可）
    if (m_hdr)
    {
        applyHdrMetadata(context);
    }
}

void Swapchain::setHdrMetadata(Context& context, float maxLuminanceNits)
{
    m_hdrPeakNits = std::max(maxLuminanceNits, 100.0f);
    applyHdrMetadata(context);
}

void Swapchain::applyHdrMetadata(Context& context)
{
    if (!m_hdr || !context.hdrMetadataSupported || m_swapchain == VK_NULL_HANDLE)
    {
        return;
    }

    VkHdrMetadataEXT metadata{VK_STRUCTURE_TYPE_HDR_METADATA_EXT};
    metadata.displayPrimaryRed = {0.708f, 0.292f};
    metadata.displayPrimaryGreen = {0.170f, 0.797f};
    metadata.displayPrimaryBlue = {0.131f, 0.046f};
    metadata.whitePoint = {0.3127f, 0.3290f};
    metadata.maxLuminance = m_hdrPeakNits;
    metadata.minLuminance = 0.05f;
    metadata.maxContentLightLevel = m_hdrPeakNits;
    metadata.maxFrameAverageLightLevel = std::min(m_hdrPeakNits, 1000.0f);
    vkSetHdrMetadataEXT(context.device, 1, &m_swapchain, &metadata);
    RT_LOG("HDR metadata: peak %.0f nits", m_hdrPeakNits);
}

void Swapchain::recreate(Context& context, uint32_t width, uint32_t height, bool preferHdr)
{
    if (width == 0 || height == 0 || context.surface == VK_NULL_HANDLE)
    {
        return;
    }

    vkDeviceWaitIdle(context.device);
    VkSwapchainKHR old = m_swapchain;
    m_swapchain = VK_NULL_HANDLE;
    for (VkImageView view : views)
    {
        vkDestroyImageView(context.device, view, nullptr);
    }
    views.clear();
    images.clear();

    create(context, width, height, preferHdr, old);
}

VkResult Swapchain::acquireNext(Context& context, VkSemaphore semaphore, uint32_t* imageIndex)
{
    return vkAcquireNextImageKHR(context.device, m_swapchain, UINT64_MAX, semaphore, VK_NULL_HANDLE, imageIndex);
}

VkResult Swapchain::present(Context& context, VkQueue queue, VkSemaphore waitSemaphore, uint32_t imageIndex)
{
    VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &waitSemaphore;
    info.swapchainCount = 1;
    info.pSwapchains = &m_swapchain;
    info.pImageIndices = &imageIndex;
    return vkQueuePresentKHR(queue, &info);
}
