#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <vector>

class Platform;

class Context
{
public:
    bool init(Platform& platform);
    void shutdown();

    bool createSurface(Platform& platform);
    void destroySurface();

    VkInstance       instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR     surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice         device = VK_NULL_HANDLE;

    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue computeQueue  = VK_NULL_HANDLE;
    VkQueue transferQueue = VK_NULL_HANDLE;
    uint32_t graphicsFamily = 0;
    uint32_t computeFamily  = 0;
    uint32_t transferFamily = 0;

    VmaAllocator allocator = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props{};
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
    VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    uint32_t apiVersion = VK_API_VERSION_1_3;
    bool     validationEnabled = false;

    bool rayQuerySupported = false;
    bool rayTracingPipelineSupported = false;

private:
    void createInstance(Platform& platform);
    void pickPhysicalDevice();
    void createDevice();
    void createAllocator();

    bool supportsRayQuery(VkPhysicalDevice device, const std::vector<VkExtensionProperties>& extensions) const;
    bool supportsRayTracingPipeline(VkPhysicalDevice device,
                                    const std::vector<VkExtensionProperties>& extensions) const;
    static bool extensionAvailable(const std::vector<VkExtensionProperties>& extensions, const char* name);
    static std::vector<VkExtensionProperties> enumerateDeviceExtensions(VkPhysicalDevice device);
};
