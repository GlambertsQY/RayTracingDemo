#include "ui/ImGuiLayer.h"

#include "common/Log.h"
#include "platform/Platform.h"
#include "vulkan/Context.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>

#if defined(__ANDROID__)
#include <android/native_window.h>
#include <imgui_impl_android.h>
#else
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#endif

namespace
{
VkInstance g_instance = VK_NULL_HANDLE;

PFN_vkVoidFunction imguiVulkanLoader(const char* functionName, void*)
{
    return vkGetInstanceProcAddr(g_instance, functionName);
}
} // namespace

void ImGuiLayer::init(Context& context, Platform& platform, VkFormat colorFormat, uint32_t imageCount)
{
    m_context = &context;
    g_instance = context.instance;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#if defined(__ANDROID__)
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
    // 移动端字体按密度放大，但限制在 2x，避免面板内容被截断
    const float uiScale = std::min(platform.uiScale(), 2.0f);
    io.FontGlobalScale = uiScale;
    ImGui::GetStyle().ScaleAllSizes(uiScale);
    ImGui_ImplAndroid_Init(static_cast<ANativeWindow*>(platform.nativeWindow()));
#else
    ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow*>(platform.nativeWindow()), true);
#endif
    ImGui::StyleColorsDark();

    ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3, imguiVulkanLoader);

    VkPipelineRenderingCreateInfo pipelineRendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    pipelineRendering.colorAttachmentCount = 1;
    pipelineRendering.pColorAttachmentFormats = &colorFormat;

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = context.instance;
    info.PhysicalDevice = context.physicalDevice;
    info.Device = context.device;
    info.QueueFamily = context.graphicsFamily;
    info.Queue = context.graphicsQueue;
    info.DescriptorPool = VK_NULL_HANDLE;
    info.DescriptorPoolSize = 1024;
    info.MinImageCount = 2;
    info.ImageCount = imageCount;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = pipelineRendering;
    info.UseDynamicRendering = true;

    if (!ImGui_ImplVulkan_Init(&info))
    {
        RT_FATAL("ImGui_ImplVulkan_Init failed");
    }
}

void ImGuiLayer::shutdown()
{
    if (!m_context)
    {
        return;
    }

    ImGui_ImplVulkan_Shutdown();
#if defined(__ANDROID__)
    ImGui_ImplAndroid_Shutdown();
#else
    ImGui_ImplGlfw_Shutdown();
#endif
    ImGui::DestroyContext();

    m_context = nullptr;
}

void ImGuiLayer::newFrame()
{
    ImGui_ImplVulkan_NewFrame();
#if defined(__ANDROID__)
    ImGui_ImplAndroid_NewFrame();
#else
    ImGui_ImplGlfw_NewFrame();
#endif
    ImGui::NewFrame();
}

void ImGuiLayer::render(VkCommandBuffer cmd)
{
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}

bool ImGuiLayer::wantCaptureMouse() const
{
    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiLayer::wantCaptureKeyboard() const
{
    return ImGui::GetIO().WantCaptureKeyboard;
}
