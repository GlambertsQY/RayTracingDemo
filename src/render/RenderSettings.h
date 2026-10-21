#pragma once

#include <glm/glm.hpp>
#include <cstdint>

// CPU/GPU 共享的每帧常量。所有 SSBO/UBO 使用 scalar layout，字段顺序即内存顺序。
struct FrameConstants
{
    glm::vec4 camPos;
    glm::vec4 camRight;
    glm::vec4 camUp;
    glm::vec4 camForward;
    glm::vec2 resolution;
    uint32_t  frameIndex;      // 已累积帧数，当前帧为第 frameIndex 帧
    uint32_t  maxBounces;
    uint32_t  samplesPerFrame;
    uint32_t  lightCount;
    uint32_t  flags;
    float     fov;             // 弧度
    glm::vec4 sunDir;          // xyz = 指向太阳的方向, w = 强度
    glm::vec4 sunColor;
    float     exposure;
    float     envIntensity;
    float     fireflyClamp;
    float     pad0;
};

struct GpuMaterial
{
    glm::vec4 baseColor;
    glm::vec4 emissive;        // rgb = 自发光, w = 透射权重(0/1)
    float     metallic;
    float     roughness;
    float     ior;
    float     alphaCutoff;
    int32_t   baseColorTex;
    int32_t   normalTex;
    int32_t   mrTex;
    int32_t   alphaMode;       // 0 = OPAQUE, 1 = MASK
};

struct GpuMesh
{
    uint32_t firstIndex;
    uint32_t indexCount;
    int32_t  baseVertex;
    uint32_t materialIndex;
};

struct GpuInstance
{
    uint32_t meshIndex;
    int32_t  materialOverride;   // < 0 时使用 mesh 自带材质
    uint32_t pad0;
    uint32_t pad1;
};

struct GpuLight
{
    glm::vec4 position;        // xyz = 中心, w = 类型 (0 = 矩形, 1 = 球)
    glm::vec4 emission;        // rgb = 辐射亮度, w = 半径
    glm::vec4 u;               // 矩形半轴 u
    glm::vec4 v;               // 矩形半轴 v
};

struct Settings
{
    int   sceneIndex     = 0;     // 0 = 程序化场景, 1 = Sponza
    int   maxBounces     = 4;
    int   samplesPerFrame = 1;
    float exposure       = 1.0f;
    float sunIntensity   = 4.0f;
    float sunAzimuth     = 40.0f;
    float sunElevation   = 45.0f;
    float envIntensity   = 0.9f;
    float fireflyClamp   = 5.0f;
    bool  showUI         = true;
};
