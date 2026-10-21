#include "vulkan/Context.h"

#include "common/Log.h"
#include "platform/Platform.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace
{
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        RT_LOG("[Vulkan] %s", data->pMessage);
    }
    return VK_FALSE;
}
} // namespace

bool Context::extensionAvailable(const std::vector<VkExtensionProperties>& extensions, const char* name)
{
    for (const auto& ext : extensions)
    {
        if (std::strcmp(ext.extensionName, name) == 0)
        {
            return true;
        }
    }
    return false;
}

std::vector<VkExtensionProperties> Context::enumerateDeviceExtensions(VkPhysicalDevice device)
{
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());
    return extensions;
}

bool Context::init(Platform& platform)
{
    if (volkInitialize() != VK_SUCCESS)
    {
        RT_LOG("volkInitialize failed: Vulkan loader not found");
        return false;
    }

    createInstance(platform);
    if (!createSurface(platform))
    {
        return false;
    }

    pickPhysicalDevice();
    if (physicalDevice == VK_NULL_HANDLE)
    {
        return false;
    }

    createDevice();
    if (device == VK_NULL_HANDLE)
    {
        return false;
    }

    createAllocator();
    if (allocator == VK_NULL_HANDLE)
    {
        return false;
    }

    RT_LOG("GPU: %s (API %u.%u.%u)", props.deviceName, VK_VERSION_MAJOR(props.apiVersion),
           VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion));
    RT_LOG("Ray tracing pipeline: %s", rayTracingPipelineSupported ? "supported" : "unavailable");
    return true;
}

void Context::createInstance(Platform& platform)
{
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layers.data());

#ifdef RTDEMO_DEBUG
    validationEnabled = false;
    for (const auto& layer : layers)
    {
        if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
        {
            validationEnabled = true;
            break;
        }
    }
#endif

    std::vector<const char*> extensions = platform.instanceExtensions();
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    const std::vector<const char*> validationLayers = {"VK_LAYER_KHRONOS_validation"};

    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "RayTracingDemo";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "RayTracingDemo";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    if (validationEnabled)
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    }

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS)
    {
        RT_FATAL("vkCreateInstance failed");
    }
    volkLoadInstance(instance);

    if (validationEnabled)
    {
        VkDebugUtilsMessengerCreateInfoEXT messengerInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messengerInfo.pfnUserCallback = debugCallback;
        vkCreateDebugUtilsMessengerEXT(instance, &messengerInfo, nullptr, &debugMessenger);
    }
}

bool Context::createSurface(Platform& platform)
{
    surface = platform.createSurface(instance);
    return surface != VK_NULL_HANDLE;
}

void Context::destroySurface()
{
    if (surface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }
}

bool Context::supportsRayQuery(VkPhysicalDevice device,
                               const std::vector<VkExtensionProperties>& extensions) const
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);
    if (properties.apiVersion < VK_API_VERSION_1_3)
    {
        return false;
    }

    return extensionAvailable(extensions, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
           extensionAvailable(extensions, VK_KHR_RAY_QUERY_EXTENSION_NAME) &&
           extensionAvailable(extensions, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) &&
           extensionAvailable(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
}

bool Context::supportsRayTracingPipeline(VkPhysicalDevice device,
                                         const std::vector<VkExtensionProperties>& extensions) const
{
    return supportsRayQuery(device, extensions) &&
           extensionAvailable(extensions, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
}

void Context::pickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0)
    {
        RT_LOG("No Vulkan physical device found");
        return;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    VkPhysicalDevice best = VK_NULL_HANDLE;
    int bestScore = -1;
    bool bestPipeline = false;

    for (VkPhysicalDevice device : devices)
    {
        const std::vector<VkExtensionProperties> extensions = enumerateDeviceExtensions(device);
        if (!supportsRayQuery(device, extensions))
        {
            RT_LOG("Skipping device without ray query support");
            continue;
        }

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, families.data());

        bool hasGraphics = false;
        VkBool32 presentSupport = VK_FALSE;
        for (uint32_t i = 0; i < queueFamilyCount; ++i)
        {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                hasGraphics = true;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
            }
        }
        if (!hasGraphics || !presentSupport)
        {
            continue;
        }

        int score = 100;
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            score = 1000;
        }
        else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        {
            score = 500;
        }

        if (score > bestScore)
        {
            bestScore = score;
            best = device;
            bestPipeline = supportsRayTracingPipeline(device, extensions);
        }
    }

    if (best == VK_NULL_HANDLE)
    {
        RT_LOG("No ray query capable physical device found");
        RT_LOG("Required: Vulkan 1.3, VK_KHR_acceleration_structure, VK_KHR_ray_query, "
               "VK_KHR_deferred_host_operations, VK_KHR_swapchain");
        return;
    }

    physicalDevice = best;
    rayQuerySupported = true;
    rayTracingPipelineSupported = bestPipeline;

    VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props2.pNext = &asProps;
    if (rayTracingPipelineSupported)
    {
        asProps.pNext = &rtProps;
    }
    vkGetPhysicalDeviceProperties2(physicalDevice, &props2);
    props = props2.properties;
    apiVersion = VK_API_VERSION_1_3;
}

void Context::createDevice()
{
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, families.data());

    graphicsFamily = UINT32_MAX;
    computeFamily = UINT32_MAX;
    transferFamily = UINT32_MAX;

    for (uint32_t i = 0; i < queueFamilyCount; ++i)
    {
        const VkQueueFlags flags = families[i].queueFlags;
        if ((flags & VK_QUEUE_GRAPHICS_BIT) && graphicsFamily == UINT32_MAX)
        {
            graphicsFamily = i;
        }
        if ((flags & VK_QUEUE_COMPUTE_BIT) && !(flags & VK_QUEUE_GRAPHICS_BIT) && computeFamily == UINT32_MAX)
        {
            computeFamily = i;
        }
        if ((flags & VK_QUEUE_TRANSFER_BIT) && !(flags & VK_QUEUE_GRAPHICS_BIT) &&
            !(flags & VK_QUEUE_COMPUTE_BIT) && transferFamily == UINT32_MAX)
        {
            transferFamily = i;
        }
    }

    if (graphicsFamily == UINT32_MAX)
    {
        RT_LOG("No graphics queue family");
        return;
    }
    if (computeFamily == UINT32_MAX)
    {
        computeFamily = graphicsFamily;
    }
    if (transferFamily == UINT32_MAX)
    {
        transferFamily = graphicsFamily;
    }

    std::set<uint32_t> uniqueFamilies = {graphicsFamily, computeFamily, transferFamily};
    const float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (uint32_t family : uniqueFamilies)
    {
        VkDeviceQueueCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        info.queueFamilyIndex = family;
        info.queueCount = 1;
        info.pQueuePriorities = &priority;
        queueInfos.push_back(info);
    }

    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    features12.bufferDeviceAddress = VK_TRUE;
    features12.scalarBlockLayout = VK_TRUE;
    features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    asFeatures.accelerationStructure = VK_TRUE;

    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    rayQueryFeatures.rayQuery = VK_TRUE;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    rtFeatures.rayTracingPipeline = VK_TRUE;

    VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features2.pNext = &features12;
    features12.pNext = &features13;
    features13.pNext = &asFeatures;
    asFeatures.pNext = &rayQueryFeatures;
    if (rayTracingPipelineSupported)
    {
        rayQueryFeatures.pNext = &rtFeatures;
    }

    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
    };
    if (rayTracingPipelineSupported)
    {
        deviceExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    }

    VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    createInfo.pNext = &features2;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS)
    {
        RT_LOG("vkCreateDevice failed");
        device = VK_NULL_HANDLE;
        return;
    }
    volkLoadDevice(device);

    vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
    vkGetDeviceQueue(device, computeFamily, 0, &computeQueue);
    vkGetDeviceQueue(device, transferFamily, 0, &transferQueue);
}

void Context::createAllocator()
{
    VmaVulkanFunctions functions{};
    VmaAllocatorCreateInfo info{};
    info.vulkanApiVersion = apiVersion;
    info.physicalDevice = physicalDevice;
    info.device = device;
    info.instance = instance;
    info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    if (vmaImportVulkanFunctionsFromVolk(&info, &functions) != VK_SUCCESS)
    {
        RT_LOG("vmaImportVulkanFunctionsFromVolk failed");
        return;
    }
    info.pVulkanFunctions = &functions;

    if (vmaCreateAllocator(&info, &allocator) != VK_SUCCESS)
    {
        RT_LOG("vmaCreateAllocator failed");
        allocator = VK_NULL_HANDLE;
    }
}

void Context::shutdown()
{
    if (allocator != VK_NULL_HANDLE)
    {
        vmaDestroyAllocator(allocator);
        allocator = VK_NULL_HANDLE;
    }
    if (device != VK_NULL_HANDLE)
    {
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }
    destroySurface();
    if (debugMessenger != VK_NULL_HANDLE)
    {
        vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        debugMessenger = VK_NULL_HANDLE;
    }
    if (instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
}
