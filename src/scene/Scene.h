#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
};

struct TextureData
{
    int width = 0;
    int height = 0;
    bool srgb = false;
    std::vector<uint8_t> pixels;   // RGBA8
};

struct Material
{
    glm::vec3 baseColor{1.0f};
    glm::vec3 emissive{0.0f};
    float     metallic = 0.0f;
    float     roughness = 1.0f;
    float     ior = 1.5f;
    float     alphaCutoff = 0.5f;
    float     transmission = 0.0f;
    int       alphaMode = 0;
    int       baseColorTex = -1;
    int       normalTex = -1;
    int       mrTex = -1;
};

struct Mesh
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    int material = 0;
};

struct Instance
{
    glm::mat4 transform{1.0f};
    int mesh = 0;
    int material = -1;
};

struct Light
{
    int       type = 0;            // 0 = 矩形, 1 = 球, 2 = 聚光灯
    glm::vec3 position{0.0f};
    glm::vec3 emission{0.0f};      // 矩形/球：辐射亮度；聚光灯：发光强度
    float     radius = 0.0f;       // 球灯半径
    glm::vec3 u{0.0f};             // 矩形半轴 u / 聚光灯方向
    glm::vec3 v{0.0f};             // 矩形半轴 v
    float     innerCos = 1.0f;     // 聚光灯内锥 cos
    float     outerCos = 0.0f;     // 聚光灯外锥 cos
};

struct Scene
{
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<TextureData> textures;
    std::vector<Instance> instances;
    std::vector<Light> lights;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};

    void computeBounds();
    glm::vec3 center() const { return (boundsMin + boundsMax) * 0.5f; }
    float radius() const { return glm::length(boundsMax - boundsMin) * 0.5f; }
};

Scene buildProceduralScene();
Scene buildShowcaseScene();
Scene buildSunlitGalleryScene();
