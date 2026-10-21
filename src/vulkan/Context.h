#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <vector>

struct GLFWwindow;

class Context
{
public:
    void init(GLFWwindow* window);
    void shutdown();

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

private:
    void createInstance(GLFWwindow* window);
    void pickPhysicalDevice();
    void createDevice();
    void createAllocator();
    bool supportsRayTracing(VkPhysicalDevice device) const;
};
