cmake_minimum_required(VERSION 3.28)
project(RayTracingDemo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

find_package(Vulkan REQUIRED)

# ---------------------------------------------------------------- third party
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${THIRD_PARTY}/glfw EXCLUDE_FROM_ALL)

add_subdirectory(${THIRD_PARTY}/volk EXCLUDE_FROM_ALL)
add_subdirectory(${THIRD_PARTY}/VulkanMemoryAllocator EXCLUDE_FROM_ALL)

add_library(imgui STATIC
  ${THIRD_PARTY}/imgui/imgui.cpp
  ${THIRD_PARTY}/imgui/imgui_draw.cpp
  ${THIRD_PARTY}/imgui/imgui_tables.cpp
  ${THIRD_PARTY}/imgui/imgui_widgets.cpp
  ${THIRD_PARTY}/imgui/backends/imgui_impl_glfw.cpp
  ${THIRD_PARTY}/imgui/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
  ${THIRD_PARTY}/imgui
  ${THIRD_PARTY}/imgui/backends
)
target_compile_definitions(imgui PUBLIC
  VK_NO_PROTOTYPES
  IMGUI_IMPL_VULKAN_NO_PROTOTYPES
  GLFW_INCLUDE_NONE
)
target_link_libraries(imgui PUBLIC glfw volk Vulkan::Headers)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${THIRD_PARTY}/stb)

add_library(tinygltf INTERFACE)
target_include_directories(tinygltf INTERFACE ${THIRD_PARTY}/tinygltf)

add_library(glm INTERFACE)
target_include_directories(glm INTERFACE ${THIRD_PARTY}/glm)

# -------------------------------------------------------------------- shaders
find_program(GLSLC NAMES glslc HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

set(SHADER_COMMON
  ${SHADER_SRC_DIR}/shared/common.glsl
  ${SHADER_SRC_DIR}/shared/brdf.glsl
)

set(SHADER_FILES
  path_tracer.rgen
  path_tracer.rchit
  path_tracer.rmiss
  alpha.rahit
  shadow.rahit
  shadow.rmiss
  accumulate.comp
  tonemap.vert
  tonemap.frag
)

set(SHADER_SPV "")
foreach(shader IN LISTS SHADER_FILES)
  set(src  ${SHADER_SRC_DIR}/${shader})
  set(out  ${SHADER_OUT_DIR}/${shader}.spv)
  add_custom_command(
    OUTPUT ${out}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUT_DIR}
    COMMAND ${GLSLC} --target-env=vulkan1.3 -I${SHADER_SRC_DIR} -o ${out} ${src}
    DEPENDS ${src} ${SHADER_COMMON}
    COMMENT "glslc ${shader}"
    VERBATIM
  )
  list(APPEND SHADER_SPV ${out})
endforeach()
add_custom_target(shaders ALL DEPENDS ${SHADER_SPV})

# ----------------------------------------------------------------- application
add_executable(RayTracingDemo
  src/main.cpp
  src/app/Application.cpp
  src/app/Camera.cpp
  src/vulkan/Context.cpp
  src/vulkan/Swapchain.cpp
  src/vulkan/Resources.cpp
  src/scene/Scene.cpp
  src/scene/GltfLoader.cpp
  src/scene/TextureLoader.cpp
  src/render/GpuScene.cpp
  src/render/AccelerationStructure.cpp
  src/render/RayTracer.cpp
  src/ui/ImGuiLayer.cpp
)
add_dependencies(RayTracingDemo shaders)

target_include_directories(RayTracingDemo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

find_path(RENDERDOC_INCLUDE_DIR renderdoc_app.h HINTS "C:/Program Files/RenderDoc" "$ENV{RENDERDOC_DIR}")
if (RENDERDOC_INCLUDE_DIR)
  target_include_directories(RayTracingDemo PRIVATE ${RENDERDOC_INCLUDE_DIR})
  target_compile_definitions(RayTracingDemo PRIVATE RTDEMO_HAVE_RENDERDOC)
  message(STATUS "RenderDoc in-app API enabled: ${RENDERDOC_INCLUDE_DIR}")
endif()

if (MSVC)
  target_compile_options(RayTracingDemo PRIVATE /utf-8)
endif()

target_compile_definitions(RayTracingDemo PRIVATE
  VK_NO_PROTOTYPES
  GLM_FORCE_RADIANS
  GLM_FORCE_DEPTH_ZERO_TO_ONE
  VMA_STATIC_VULKAN_FUNCTIONS=0
  VMA_DYNAMIC_VULKAN_FUNCTIONS=1
  TINYGLTF_NO_STB_IMAGE
  TINYGLTF_NO_STB_IMAGE_WRITE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  RTDEMO_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)
target_compile_definitions(RayTracingDemo PRIVATE $<$<CONFIG:Debug>:RTDEMO_DEBUG>)

target_link_libraries(RayTracingDemo PRIVATE
  imgui
  glfw
  volk
  VulkanMemoryAllocator
  tinygltf
  stb
  glm
  Vulkan::Headers
)

set_property(TARGET RayTracingDemo PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT RayTracingDemo)

add_custom_command(TARGET RayTracingDemo POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${SHADER_OUT_DIR} $<TARGET_FILE_DIR:RayTracingDemo>/shaders
  COMMENT "Copying compiled shaders"
)

add_custom_target(fetch_assets
  COMMAND powershell -ExecutionPolicy Bypass -File ${CMAKE_CURRENT_SOURCE_DIR}/scripts/fetch_assets.ps1
  COMMENT "Downloading Sponza asset"
)
