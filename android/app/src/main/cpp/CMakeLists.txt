cmake_minimum_required(VERSION 3.22)
project(RayTracingDemoAndroid LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(RTDEMO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../../" ABSOLUTE)
set(THIRD_PARTY "${RTDEMO_ROOT}/third_party")
include(${RTDEMO_ROOT}/cmake/CommonSources.cmake)

# ---------------------------------------------------------------- third party
set(VOLK_PULL_IN_VULKAN OFF CACHE BOOL "" FORCE)
set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_ANDROID_KHR CACHE STRING "" FORCE)
add_subdirectory(${THIRD_PARTY}/volk ${CMAKE_CURRENT_BINARY_DIR}/volk EXCLUDE_FROM_ALL)

add_library(VulkanMemoryAllocator INTERFACE)
target_include_directories(VulkanMemoryAllocator INTERFACE ${THIRD_PARTY}/VulkanMemoryAllocator/include)

add_library(imgui STATIC
  ${THIRD_PARTY}/imgui/imgui.cpp
  ${THIRD_PARTY}/imgui/imgui_draw.cpp
  ${THIRD_PARTY}/imgui/imgui_tables.cpp
  ${THIRD_PARTY}/imgui/imgui_widgets.cpp
  ${THIRD_PARTY}/imgui/backends/imgui_impl_android.cpp
  ${THIRD_PARTY}/imgui/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
  ${THIRD_PARTY}/imgui
  ${THIRD_PARTY}/imgui/backends
)
target_compile_definitions(imgui PUBLIC
  VK_NO_PROTOTYPES
  IMGUI_IMPL_VULKAN_NO_PROTOTYPES
  VK_USE_PLATFORM_ANDROID_KHR
)
target_link_libraries(imgui PUBLIC volk android)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${THIRD_PARTY}/stb)

add_library(tinygltf INTERFACE)
target_include_directories(tinygltf INTERFACE ${THIRD_PARTY}/tinygltf)

add_library(glm INTERFACE)
target_include_directories(glm INTERFACE ${THIRD_PARTY}/glm)

add_library(native_app_glue STATIC
  ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)
target_include_directories(native_app_glue PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue)
target_link_libraries(native_app_glue PUBLIC android log)

# -------------------------------------------------------------------- shaders
set(SHADER_SRC_DIR ${RTDEMO_ROOT}/shaders)
set(SHADER_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(EMBED_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/shader_data.cpp)

if(NOT ANDROID_HOST_TAG)
  if(CMAKE_HOST_WIN32)
    set(ANDROID_HOST_TAG windows-x86_64)
  elseif(CMAKE_HOST_APPLE)
    set(ANDROID_HOST_TAG darwin-x86_64)
  else()
    set(ANDROID_HOST_TAG linux-x86_64)
  endif()
endif()

find_program(GLSLC NAMES glslc
  HINTS "$ENV{VULKAN_SDK}/Bin" "${ANDROID_NDK}/shader-tools/${ANDROID_HOST_TAG}"
)
if(NOT GLSLC)
  message(FATAL_ERROR "glslc not found. Install the Vulkan SDK or set VULKAN_SDK.")
endif()

set(SHADER_COMMON
  ${SHADER_SRC_DIR}/shared/common.glsl
  ${SHADER_SRC_DIR}/shared/brdf.glsl
  ${SHADER_SRC_DIR}/shared/hdr.glsl
  ${SHADER_SRC_DIR}/shared/volumetric.glsl
  ${SHADER_SRC_DIR}/shared/lighting.glsl
)

set(SHADER_FILES
  path_tracer.rgen
  path_tracer.rchit
  path_tracer.rmiss
  alpha.rahit
  shadow.rahit
  shadow.rmiss
  accumulate.comp
  bloom_bright.comp
  bloom_blur_h.comp
  bloom_blur_v.comp
  hdr_stats.comp
  tonemap.vert
  tonemap.frag
  imgui_hdr.frag
  path_tracer.comp
)

set(SHADER_SPV "")
foreach(shader IN LISTS SHADER_FILES)
  set(src ${SHADER_SRC_DIR}/${shader})
  set(out ${SHADER_OUT_DIR}/${shader}.spv)
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

add_custom_command(
  OUTPUT ${EMBED_CPP}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
  COMMAND ${CMAKE_COMMAND} -DSPV_DIR=${SHADER_OUT_DIR} -DOUTPUT_CPP=${EMBED_CPP}
          -P ${RTDEMO_ROOT}/cmake/embed_shaders.cmake
  DEPENDS ${SHADER_SPV} ${RTDEMO_ROOT}/cmake/embed_shaders.cmake
  COMMENT "Embedding SPIR-V shaders"
  VERBATIM
)

add_custom_target(shaders ALL DEPENDS ${EMBED_CPP})
set_source_files_properties(${EMBED_CPP} PROPERTIES GENERATED TRUE)

# ----------------------------------------------------------------- application
add_library(raytracingdemo SHARED
  ${RTDEMO_COMMON_SOURCES}
  ${RTDEMO_ROOT}/src/platform/AndroidMain.cpp
  ${EMBED_CPP}
)
add_dependencies(raytracingdemo shaders)

target_include_directories(raytracingdemo PRIVATE
  ${RTDEMO_ROOT}/src
)

target_compile_definitions(raytracingdemo PRIVATE
  VK_NO_PROTOTYPES
  VK_USE_PLATFORM_ANDROID_KHR
  GLM_FORCE_RADIANS
  GLM_FORCE_DEPTH_ZERO_TO_ONE
  VMA_STATIC_VULKAN_FUNCTIONS=0
  VMA_DYNAMIC_VULKAN_FUNCTIONS=1
  TINYGLTF_NO_STB_IMAGE
  TINYGLTF_NO_STB_IMAGE_WRITE
)

target_link_libraries(raytracingdemo PRIVATE
  imgui
  volk
  VulkanMemoryAllocator
  native_app_glue
  tinygltf
  stb
  glm
  android
  nativewindow
  log
  vulkan
)

target_link_options(raytracingdemo PRIVATE "-uANativeActivity_onCreate" "-Wl,-z,max-page-size=16384")
