#ifndef RT_COMMON_GLSL
#define RT_COMMON_GLSL

#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require

const float kPi = 3.14159265358979323846;
const float kInvPi = 0.31830988618379067154;

struct Vertex
{
    vec3 position;
    vec3 normal;
    vec2 uv;
    vec4 tangent;
};

struct GpuMaterial
{
    vec4 baseColor;
    vec4 emissive;      // rgb = 自发光, w = 透射权重
    float metallic;
    float roughness;
    float ior;
    float alphaCutoff;
    int baseColorTex;
    int normalTex;
    int mrTex;
    int alphaMode;
};

struct GpuMesh
{
    uint firstIndex;
    uint indexCount;
    int baseVertex;
    uint materialIndex;
};

struct GpuInstance
{
    uint meshIndex;
    int materialOverride;
    uint pad0;
    uint pad1;
};

struct GpuLight
{
    vec4 position;      // xyz = 中心, w = 类型 (0 = 矩形, 1 = 球, 2 = 聚光灯)
    vec4 emission;      // rgb = 辐射亮度, w = 半径
    vec4 u;
    vec4 v;
    vec4 sampling;      // x = 归一化 CDF 上界, y = 相对功率
};

struct FrameConstants
{
    vec4 camPos;
    vec4 camRight;
    vec4 camUp;
    vec4 camForward;
    vec2 resolution;
    uint frameIndex;
    uint maxBounces;
    uint samplesPerFrame;
    uint lightCount;
    uint flags;
    float fov;
    vec4 sunDir;
    vec4 sunColor;
    float exposure;
    float envIntensity;
    float fireflyClamp;
    float pad0;
    vec2 outputResolution;
    uint neeBounces;
    float paperWhiteNits;   // HDR 输出：SDR 参考白亮度
    float hdrPeakNits;      // HDR 输出：峰值亮度
    float fogDensity;       // 体积雾散射系数
    float fogAlbedo;        // 体积雾单次散射反照率
    float fogAnisotropy;    // HG 相函数各向异性
    float fogDistance;      // 有效雾深度（场景尺度）
    float bloomStrength;    // bloom 叠加强度
};

layout(scalar, set = 0, binding = 1) uniform FrameConstantsBlock
{
    FrameConstants fc;
};

layout(scalar, set = 0, binding = 2) readonly buffer VertexBuffer
{
    Vertex gVertices[];
};

layout(scalar, set = 0, binding = 3) readonly buffer IndexBuffer
{
    uint gIndices[];
};

layout(scalar, set = 0, binding = 4) readonly buffer MeshBuffer
{
    GpuMesh gMeshes[];
};

layout(scalar, set = 0, binding = 5) readonly buffer InstanceBuffer
{
    GpuInstance gInstances[];
};

layout(scalar, set = 0, binding = 6) readonly buffer MaterialBuffer
{
    GpuMaterial gMaterials[];
};

layout(scalar, set = 0, binding = 7) readonly buffer LightBuffer
{
    GpuLight gLights[];
};

layout(set = 0, binding = 8) uniform sampler2D gTextures[128];

layout(set = 0, binding = 9, rgba16f) uniform image2D gRadianceImage;
layout(set = 0, binding = 10, rgba32f) uniform image2D gAccumImage;

struct HitPayload
{
    vec3 normal;
    float t;
    vec2 uv;
    uint instance;
    uint primitive;
};

// ------------------------------------------------------------------- 随机数
uint gSeed;

bool isFiniteVec(vec3 v)
{
    return !any(isnan(v)) && !any(isinf(v));
}

uint pcgHash(uint value)
{
    uint state = value * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float rand()
{
    gSeed = gSeed * 747796405u + 2891336453u;
    uint word = ((gSeed >> ((gSeed >> 28u) + 4u)) ^ gSeed) * 277803737u;
    word = (word >> 22u) ^ word;
    return float(word) * (1.0 / 4294967296.0);
}

void seedRng(uvec3 launch, uint sampleIndex)
{
    gSeed = pcgHash(launch.x + launch.y * 1973u + launch.z * 9277u + fc.frameIndex * 26699u +
                    sampleIndex * 5783u + 1u);
    rand();
}

// R2 低差异序列 + 每像素随机旋转：比纯白噪声抖动有更均匀的采样分布
vec2 lowDiscrepancyJitter(uvec2 pixel, uint sampleIndex)
{
    const vec2 g = vec2(0.7548776662466927, 0.5698402909980532);
    const float offsetX = float(pcgHash(pixel.x * 1973u + pixel.y * 9277u)) * (1.0 / 4294967296.0);
    const float offsetY = float(pcgHash(pixel.x * 26699u + pixel.y * 5783u)) * (1.0 / 4294967296.0);
    return fract(g * float(sampleIndex % 4096u) + vec2(offsetX, offsetY));
}

vec3 randomUnitVector()
{
    const float z = rand() * 2.0 - 1.0;
    const float angle = rand() * 2.0 * kPi;
    const float r = sqrt(max(0.0, 1.0 - z * z));
    return vec3(r * cos(angle), r * sin(angle), z);
}

void buildOnb(vec3 n, out vec3 tangent, out vec3 bitangent)
{
    const vec3 helper = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    tangent = normalize(cross(helper, n));
    bitangent = cross(n, tangent);
}

vec3 cosineHemisphere(vec3 n)
{
    const float r1 = rand();
    const float r2 = rand();
    const float phi = 2.0 * kPi * r1;
    const float radius = sqrt(r2);
    const float z = sqrt(max(0.0, 1.0 - r2));
    vec3 tangent;
    vec3 bitangent;
    buildOnb(n, tangent, bitangent);
    return normalize(tangent * (radius * cos(phi)) + bitangent * (radius * sin(phi)) + n * z);
}

// ------------------------------------------------------------------- 几何
GpuMaterial getMaterial(uint instanceIndex)
{
    const GpuInstance instance = gInstances[instanceIndex];
    const uint materialIndex =
        instance.materialOverride >= 0 ? uint(instance.materialOverride) : gMeshes[instance.meshIndex].materialIndex;
    return gMaterials[materialIndex];
}

void fetchTriangle(uint instanceIndex, uint primitiveIndex, out vec3 p0, out vec3 p1, out vec3 p2, out vec3 n0,
                   out vec3 n1, out vec3 n2, out vec2 uv0, out vec2 uv1, out vec2 uv2)
{
    const GpuInstance instance = gInstances[instanceIndex];
    const GpuMesh mesh = gMeshes[instance.meshIndex];
    const uint base = mesh.firstIndex + primitiveIndex * 3u;
    const uint i0 = gIndices[base + 0u];
    const uint i1 = gIndices[base + 1u];
    const uint i2 = gIndices[base + 2u];
    const Vertex v0 = gVertices[i0];
    const Vertex v1 = gVertices[i1];
    const Vertex v2 = gVertices[i2];
    p0 = v0.position;
    p1 = v1.position;
    p2 = v2.position;
    n0 = v0.normal;
    n1 = v1.normal;
    n2 = v2.normal;
    uv0 = v0.uv;
    uv1 = v1.uv;
    uv2 = v2.uv;
}

vec3 objectToWorldPoint(mat4x3 transform, vec3 p)
{
    return transform * vec4(p, 1.0);
}

vec3 objectToWorldNormal(mat4x3 transform, vec3 n)
{
    return normalize(mat3(transform[0], transform[1], transform[2]) * n);
}

vec3 barycentric(vec3 p, vec3 a, vec3 b, vec3 c)
{
    const vec3 v0 = b - a;
    const vec3 v1 = c - a;
    const vec3 v2 = p - a;
    const float d00 = dot(v0, v0);
    const float d01 = dot(v0, v1);
    const float d11 = dot(v1, v1);
    const float d20 = dot(v2, v0);
    const float d21 = dot(v2, v1);
    const float denom = max(d00 * d11 - d01 * d01, 1e-12);
    const float v = (d11 * d20 - d01 * d21) / denom;
    const float w = (d00 * d21 - d01 * d20) / denom;
    return vec3(1.0 - v - w, v, w);
}

vec4 sampleTexture(int index, vec2 uv)
{
    return texture(gTextures[nonuniformEXT(index)], uv);
}

// -------------------------------------------------------------------- 光
vec3 skyRadiance(vec3 direction)
{
    const float y = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 ground = vec3(0.16, 0.15, 0.14);
    vec3 horizon = vec3(0.62, 0.68, 0.78);
    vec3 zenith = vec3(0.20, 0.36, 0.70);
    if ((fc.flags & 2u) != 0u)
    {
        // 夕阳天空：地平线暖橙、天顶偏紫
        ground = vec3(0.18, 0.10, 0.07);
        horizon = vec3(1.35, 0.55, 0.22);
        zenith = vec3(0.16, 0.15, 0.30);
    }
    vec3 color = mix(ground, horizon, smoothstep(0.40, 0.52, y));
    color = mix(color, zenith, smoothstep(0.52, 0.95, y));
    color *= fc.envIntensity;

    // 太阳盘与光晕：保证画面中存在真正的峰值像素（配合 bloom 产生眩光）
    const vec3 sunDir = normalize(fc.sunDir.xyz);
    const float cosSun = clamp(dot(direction, sunDir), -1.0, 1.0);
    const float disk = smoothstep(0.99975, 0.99992, cosSun);
    const float glow = 0.20 * pow(max(cosSun, 0.0), 2048.0) + 0.04 * pow(max(cosSun, 0.0), 64.0);
    color += (disk * 20.0 + glow) * fc.sunColor.rgb * fc.sunColor.w;

    return color;
}

float fresnelDielectric(float cosTheta, float ior)
{
    const float f0 = pow((ior - 1.0) / (ior + 1.0), 2.0);
    return f0 + (1.0 - f0) * pow(1.0 - cosTheta, 5.0);
}

float fresnelSchlick(float cosTheta, vec3 f0)
{
    return f0.x + (1.0 - f0.x) * pow(1.0 - cosTheta, 5.0);
}

vec3 fresnelSchlickVec(float cosTheta, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(1.0 - cosTheta, 5.0);
}

float distributionGgx(float nDotH, float roughness)
{
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float denom = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(kPi * denom * denom, 1e-6);
}

float geometrySchlickGgx(float nDotX, float roughness)
{
    const float k = roughness * roughness * 0.5;
    return nDotX / max(nDotX * (1.0 - k) + k, 1e-6);
}

vec3 sampleGgxHalfVector(vec3 n, float roughness)
{
    const float a = roughness * roughness;
    const float u1 = rand();
    const float u2 = rand();
    const float phi = 2.0 * kPi * u1;
    const float cosTheta = sqrt(max(0.0, (1.0 - u2) / (1.0 + (a * a - 1.0) * u2)));
    const float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    vec3 tangent;
    vec3 bitangent;
    buildOnb(n, tangent, bitangent);
    return normalize(tangent * (sinTheta * cos(phi)) + bitangent * (sinTheta * sin(phi)) + n * cosTheta);
}

vec3 acesTonemap(vec3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

#endif // RT_COMMON_GLSL
