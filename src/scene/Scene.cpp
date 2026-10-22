#include "scene/Scene.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <utility>

namespace
{
constexpr float kPi = 3.14159265358979323846f;

Material makeMaterial(const glm::vec3& color, float roughness, float metallic = 0.0f, float transmission = 0.0f,
                      float ior = 1.5f)
{
    Material m;
    m.baseColor = color;
    m.roughness = roughness;
    m.metallic = metallic;
    m.transmission = transmission;
    m.ior = ior;
    return m;
}

void addQuad(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, const glm::vec3& p0, const glm::vec3& p1,
             const glm::vec3& p2, const glm::vec3& p3, const glm::vec3& normal, const glm::vec4& tangent)
{
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    vertices.push_back({p0, normal, {0.0f, 1.0f}, tangent});
    vertices.push_back({p1, normal, {1.0f, 1.0f}, tangent});
    vertices.push_back({p2, normal, {1.0f, 0.0f}, tangent});
    vertices.push_back({p3, normal, {0.0f, 0.0f}, tangent});
    indices.insert(indices.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
}

Mesh makeBox()
{
    Mesh mesh;
    const glm::vec3 p = glm::vec3(0.5f);

    // +X
    addQuad(mesh.vertices, mesh.indices, {p.x, -p.y, -p.z}, {p.x, p.y, -p.z}, {p.x, p.y, p.z}, {p.x, -p.y, p.z},
            {1, 0, 0}, {0, 0, -1, 1});
    // -X
    addQuad(mesh.vertices, mesh.indices, {-p.x, -p.y, p.z}, {-p.x, p.y, p.z}, {-p.x, p.y, -p.z}, {-p.x, -p.y, -p.z},
            {-1, 0, 0}, {0, 0, 1, 1});
    // +Y
    addQuad(mesh.vertices, mesh.indices, {-p.x, p.y, -p.z}, {-p.x, p.y, p.z}, {p.x, p.y, p.z}, {p.x, p.y, -p.z},
            {0, 1, 0}, {1, 0, 0, 1});
    // -Y
    addQuad(mesh.vertices, mesh.indices, {-p.x, -p.y, p.z}, {-p.x, -p.y, -p.z}, {p.x, -p.y, -p.z}, {p.x, -p.y, p.z},
            {0, -1, 0}, {1, 0, 0, 1});
    // +Z
    addQuad(mesh.vertices, mesh.indices, {p.x, -p.y, p.z}, {p.x, p.y, p.z}, {-p.x, p.y, p.z}, {-p.x, -p.y, p.z},
            {0, 0, 1}, {-1, 0, 0, 1});
    // -Z
    addQuad(mesh.vertices, mesh.indices, {-p.x, -p.y, -p.z}, {-p.x, p.y, -p.z}, {p.x, p.y, -p.z}, {p.x, -p.y, -p.z},
            {0, 0, -1}, {1, 0, 0, 1});
    return mesh;
}

Mesh makeSphere(int slices, int stacks)
{
    Mesh mesh;
    for (int y = 0; y <= stacks; ++y)
    {
        const float v = static_cast<float>(y) / static_cast<float>(stacks);
        const float theta = v * kPi;
        for (int x = 0; x <= slices; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(slices);
            const float phi = u * 2.0f * kPi;
            const glm::vec3 n(std::cos(phi) * std::sin(theta), std::cos(theta), std::sin(phi) * std::sin(theta));
            mesh.vertices.push_back(
                {n * 0.5f, n, {u, 1.0f - v}, {-std::sin(phi), 0.0f, std::cos(phi), 1.0f}});
        }
    }

    for (int y = 0; y < stacks; ++y)
    {
        for (int x = 0; x < slices; ++x)
        {
            const uint32_t row = static_cast<uint32_t>(slices + 1);
            const uint32_t i0 = static_cast<uint32_t>(y) * row + x;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + row;
            const uint32_t i3 = i2 + 1;
            mesh.indices.insert(mesh.indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }
    return mesh;
}

Mesh makeTorus(int majorSegments, int minorSegments, float majorRadius, float minorRadius)
{
    Mesh mesh;
    for (int i = 0; i <= majorSegments; ++i)
    {
        const float u = static_cast<float>(i) / static_cast<float>(majorSegments);
        const float phi = u * 2.0f * kPi;
        const glm::vec3 center(std::cos(phi) * majorRadius, 0.0f, std::sin(phi) * majorRadius);
        for (int j = 0; j <= minorSegments; ++j)
        {
            const float v = static_cast<float>(j) / static_cast<float>(minorSegments);
            const float theta = v * 2.0f * kPi;
            const glm::vec3 normal(std::cos(phi) * std::cos(theta), std::sin(theta), std::sin(phi) * std::cos(theta));
            const glm::vec3 position = center + normal * minorRadius;
            const glm::vec3 tangent(-std::sin(phi), 0.0f, std::cos(phi));
            mesh.vertices.push_back({position, normal, {u, v}, glm::vec4(tangent, 1.0f)});
        }
    }

    for (int i = 0; i < majorSegments; ++i)
    {
        for (int j = 0; j < minorSegments; ++j)
        {
            const uint32_t row = static_cast<uint32_t>(minorSegments + 1);
            const uint32_t i0 = static_cast<uint32_t>(i) * row + j;
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + row;
            const uint32_t i3 = i2 + 1;
            mesh.indices.insert(mesh.indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }
    return mesh;
}

void finalizeTangents(Mesh& mesh)
{
    for (auto& v : mesh.vertices)
    {
        glm::vec3 t(v.tangent);
        t -= v.normal * glm::dot(v.normal, t);
        const float len = glm::length(t);
        v.tangent = glm::vec4(len > 1e-6f ? t / len : glm::vec3(1, 0, 0), 1.0f);
    }
}
} // namespace

void Scene::computeBounds()
{
    boundsMin = glm::vec3(std::numeric_limits<float>::max());
    boundsMax = glm::vec3(-std::numeric_limits<float>::max());

    if (instances.empty())
    {
        boundsMin = glm::vec3(-1.0f);
        boundsMax = glm::vec3(1.0f);
        return;
    }

    std::vector<std::pair<glm::vec3, glm::vec3>> meshBounds(meshes.size());
    for (size_t i = 0; i < meshes.size(); ++i)
    {
        glm::vec3 mn(std::numeric_limits<float>::max());
        glm::vec3 mx(-std::numeric_limits<float>::max());
        for (const auto& v : meshes[i].vertices)
        {
            mn = glm::min(mn, v.position);
            mx = glm::max(mx, v.position);
        }
        meshBounds[i] = {mn, mx};
    }

    for (const auto& inst : instances)
    {
        const auto& mb = meshBounds[static_cast<size_t>(inst.mesh)];
        for (int corner = 0; corner < 8; ++corner)
        {
            const glm::vec3 local(corner & 1 ? mb.second.x : mb.first.x, corner & 2 ? mb.second.y : mb.first.y,
                                  corner & 4 ? mb.second.z : mb.first.z);
            const glm::vec3 world = glm::vec3(inst.transform * glm::vec4(local, 1.0f));
            boundsMin = glm::min(boundsMin, world);
            boundsMax = glm::max(boundsMax, world);
        }
    }
}

Scene buildProceduralScene()
{
    Scene scene;

    // --- 材质
    const int matFloor = static_cast<int>(scene.materials.size());
    scene.materials.push_back(makeMaterial({0.62f, 0.62f, 0.60f}, 0.75f));
    const int matWallWarm = static_cast<int>(scene.materials.size());
    scene.materials.push_back(makeMaterial({0.72f, 0.68f, 0.60f}, 0.85f));
    const int matRed = static_cast<int>(scene.materials.size());
    scene.materials.push_back(makeMaterial({0.62f, 0.12f, 0.10f}, 0.7f));
    const int matGreen = static_cast<int>(scene.materials.size());
    scene.materials.push_back(makeMaterial({0.10f, 0.50f, 0.16f}, 0.7f));
    const int matCeiling = static_cast<int>(scene.materials.size());
    scene.materials.push_back(makeMaterial({0.80f, 0.80f, 0.80f}, 0.9f));
    const int matLight = static_cast<int>(scene.materials.size());
    {
        Material m = makeMaterial({0.0f, 0.0f, 0.0f}, 1.0f);
        m.emissive = {9.0f, 8.0f, 6.0f};
        scene.materials.push_back(m);
    }

    const int gold = static_cast<int>(scene.materials.size());
    scene.materials.push_back(makeMaterial({1.0f, 0.78f, 0.34f}, 0.08f, 1.0f));
    const int copper = static_cast<int>(scene.materials.size());
    scene.materials.push_back(makeMaterial({0.95f, 0.52f, 0.30f}, 0.18f, 1.0f));
    const int aluminum = static_cast<int>(scene.materials.size());
    scene.materials.push_back(makeMaterial({0.90f, 0.92f, 0.95f}, 0.05f, 1.0f));
    const int glass = static_cast<int>(scene.materials.size());
    scene.materials.push_back(makeMaterial({1.0f, 1.0f, 1.0f}, 0.0f, 0.0f, 1.0f, 1.5f));
    const int blue = static_cast<int>(scene.materials.size());
    scene.materials.push_back(makeMaterial({0.12f, 0.28f, 0.78f}, 0.45f));
    const int orange = static_cast<int>(scene.materials.size());
    scene.materials.push_back(makeMaterial({0.90f, 0.42f, 0.10f}, 0.55f));
    const int whitePlastic = static_cast<int>(scene.materials.size());
    scene.materials.push_back(makeMaterial({0.85f, 0.85f, 0.85f}, 0.25f));
    const int darkRubber = static_cast<int>(scene.materials.size());
    scene.materials.push_back(makeMaterial({0.06f, 0.06f, 0.07f}, 0.65f));

    // --- 网格
    const int boxMesh = static_cast<int>(scene.meshes.size());
    scene.meshes.push_back(makeBox());
    const int sphereMesh = static_cast<int>(scene.meshes.size());
    scene.meshes.push_back(makeSphere(48, 24));
    const int torusMesh = static_cast<int>(scene.meshes.size());
    scene.meshes.push_back(makeTorus(48, 24, 0.35f, 0.14f));
    for (auto& mesh : scene.meshes)
    {
        finalizeTangents(mesh);
    }

    auto addInstance = [&](const glm::mat4& transform, int meshIndex, int materialIndex) {
        Instance inst;
        inst.transform = transform;
        inst.mesh = meshIndex;
        inst.material = materialIndex;
        scene.instances.push_back(inst);
    };

    // --- 房间（10 x 6 x 10，正面开放）
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(0, -0.05f, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(10.0f, 0.1f, 10.0f)), boxMesh,
                matFloor);
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(0, 3.0f, -5.05f)) * glm::scale(glm::mat4(1.0f), glm::vec3(10.0f, 6.0f, 0.1f)), boxMesh,
                matWallWarm);
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(-5.05f, 3.0f, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 6.0f, 10.0f)), boxMesh,
                matRed);
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(5.05f, 3.0f, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 6.0f, 10.0f)), boxMesh,
                matGreen);
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(0, 6.05f, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(10.0f, 0.1f, 10.0f)), boxMesh,
                matCeiling);
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(0, 5.94f, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(3.4f, 0.04f, 3.4f)), boxMesh,
                matLight);

    // --- 512 个实例组成的"波形阵列"
    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    const int gridN = 22;
    for (int z = 0; z < gridN; ++z)
    {
        for (int x = 0; x < gridN; ++x)
        {
            const float fx = static_cast<float>(x) / (gridN - 1);
            const float fz = static_cast<float>(z) / (gridN - 1);
            const float px = (fx - 0.5f) * 8.6f + (unit(rng) - 0.5f) * 0.12f;
            const float pz = (fz - 0.5f) * 8.6f + (unit(rng) - 0.5f) * 0.12f;
            const float wave = 0.25f + 0.55f * std::abs(std::sin(fx * 9.0f) * std::cos(fz * 7.0f));
            const float s = 0.16f + 0.22f * unit(rng);

            int meshIndex = sphereMesh;
            const float pick = unit(rng);
            if (pick < 0.45f)
            {
                meshIndex = sphereMesh;
            }
            else if (pick < 0.75f)
            {
                meshIndex = boxMesh;
            }
            else
            {
                meshIndex = torusMesh;
            }

            int materialIndex = blue;
            const float matPick = unit(rng);
            if (matPick < 0.36f)
            {
                materialIndex = blue;
            }
            else if (matPick < 0.56f)
            {
                materialIndex = orange;
            }
            else if (matPick < 0.70f)
            {
                materialIndex = whitePlastic;
            }
            else if (matPick < 0.80f)
            {
                materialIndex = darkRubber;
            }
            else if (matPick < 0.88f)
            {
                materialIndex = gold;
            }
            else if (matPick < 0.94f)
            {
                materialIndex = copper;
            }
            else if (matPick < 0.97f)
            {
                materialIndex = aluminum;
            }
            else
            {
                materialIndex = glass;
            }

            glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(px, wave + s * 0.5f, pz));
            transform = glm::rotate(transform, unit(rng) * 6.2831853f, glm::vec3(0, 1, 0));
            transform = glm::rotate(transform, unit(rng) * 6.2831853f, glm::vec3(1, 0, 0));
            transform = glm::scale(transform, glm::vec3(s));
            addInstance(transform, meshIndex, materialIndex);
        }
    }

    // --- 5 个大型玻璃 / 金属球 + 底座
    const glm::vec3 heroPositions[5] = {{-2.6f, 0, 1.6f}, {-1.3f, 0, 1.6f}, {0.0f, 0, 1.6f},
                                        {1.3f, 0, 1.6f},  {2.6f, 0, 1.6f}};
    const int heroMaterials[5] = {glass, gold, glass, copper, aluminum};
    for (int i = 0; i < 5; ++i)
    {
        const glm::vec3 p = heroPositions[i];
        addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(p.x, 0.35f, p.z)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.55f, 0.7f, 0.55f)), boxMesh,
                    whitePlastic);
        addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(p.x, 1.15f, p.z)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.9f)), sphereMesh,
                    heroMaterials[i]);
    }

    // --- 灯光
    Light ceilingLight;
    ceilingLight.type = 0;
    ceilingLight.position = glm::vec3(0.0f, 5.90f, 0.0f);
    ceilingLight.emission = glm::vec3(7.0f, 6.0f, 4.8f);
    ceilingLight.u = glm::vec3(1.7f, 0.0f, 0.0f);
    ceilingLight.v = glm::vec3(0.0f, 0.0f, 1.7f);
    scene.lights.push_back(ceilingLight);

    // --- 4 盏彩色球形灯：为 HDR 提供高亮峰值与色溢，并放置发光球体作为可见光源
    struct AccentLight
    {
        glm::vec3 position;
        glm::vec3 emission;
    };
    const AccentLight accentLights[4] = {
        {{-2.8f, 3.4f, -2.8f}, {18.0f, 6.0f, 1.5f}},   // 暖琥珀
        {{2.8f, 3.4f, -2.8f}, {1.5f, 9.0f, 18.0f}},    // 青
        {{-2.8f, 3.4f, 2.8f}, {17.0f, 2.0f, 9.0f}},    // 洋红
        {{2.8f, 3.4f, 2.8f}, {6.0f, 13.0f, 18.0f}},    // 冷白
    };
    for (const AccentLight& accent : accentLights)
    {
        Light light;
        light.type = 1;
        light.position = accent.position;
        light.emission = accent.emission;
        light.radius = 0.28f;
        scene.lights.push_back(light);

        Material bulb = makeMaterial({0.0f, 0.0f, 0.0f}, 1.0f);
        bulb.emissive = accent.emission * 0.6f;
        const int bulbMaterial = static_cast<int>(scene.materials.size());
        scene.materials.push_back(bulb);

        addInstance(glm::translate(glm::mat4(1.0f), accent.position) * glm::scale(glm::mat4(1.0f), glm::vec3(0.3f)),
                    sphereMesh, bulbMaterial);
    }

    scene.computeBounds();
    return scene;
}

// HDR 展示场景：低角度夕阳 + 体积雾 + 三档亮度（灯板 / 灯球 / 聚光灯）
Scene buildShowcaseScene()
{
    Scene scene;

    auto addMaterial = [&](const glm::vec3& color, float roughness, float metallic = 0.0f) {
        const int index = static_cast<int>(scene.materials.size());
        scene.materials.push_back(makeMaterial(color, roughness, metallic));
        return index;
    };
    auto addEmissiveMaterial = [&](const glm::vec3& emission) {
        Material material = makeMaterial({0.0f, 0.0f, 0.0f}, 1.0f);
        material.emissive = emission;
        const int index = static_cast<int>(scene.materials.size());
        scene.materials.push_back(material);
        return index;
    };

    const int matFloor = addMaterial({0.10f, 0.10f, 0.11f}, 0.35f);
    const int matWall = addMaterial({0.16f, 0.15f, 0.15f}, 0.85f);
    const int matCeiling = addMaterial({0.08f, 0.08f, 0.09f}, 0.90f);
    const int matMetal = addMaterial({0.92f, 0.93f, 0.96f}, 0.12f, 1.0f);
    const int matPedestal = addMaterial({0.22f, 0.22f, 0.24f}, 0.50f);

    // 左 -> 右：弱 / 强 / 极强，便于直观对比 HDR 高光
    const glm::vec3 panelEmissive[3] = {{1.5f, 1.5f, 1.5f}, {15.0f, 15.0f, 15.0f}, {150.0f, 150.0f, 150.0f}};
    const glm::vec3 ballEmissive[3] = {{4.0f, 3.2f, 2.4f}, {40.0f, 32.0f, 24.0f}, {400.0f, 320.0f, 240.0f}};
    const glm::vec3 spotIntensity[3] = {{15.0f, 15.0f, 16.0f}, {150.0f, 150.0f, 160.0f}, {1500.0f, 1500.0f, 1600.0f}};

    int panelMaterial[3];
    int ballMaterial[3];
    int spotMaterial[3];
    for (int i = 0; i < 3; ++i)
    {
        panelMaterial[i] = addEmissiveMaterial(panelEmissive[i]);
        ballMaterial[i] = addEmissiveMaterial(ballEmissive[i]);
        spotMaterial[i] = addEmissiveMaterial(glm::vec3(60.0f));
    }

    // --- 网格
    const int boxMesh = static_cast<int>(scene.meshes.size());
    scene.meshes.push_back(makeBox());
    const int sphereMesh = static_cast<int>(scene.meshes.size());
    scene.meshes.push_back(makeSphere(48, 24));
    for (auto& mesh : scene.meshes)
    {
        finalizeTangents(mesh);
    }

    auto addInstance = [&](const glm::mat4& transform, int meshIndex, int materialIndex) {
        Instance inst;
        inst.transform = transform;
        inst.mesh = meshIndex;
        inst.material = materialIndex;
        scene.instances.push_back(inst);
    };

    // --- 房间 16 x 8 x 18（z: -11 ~ +7），正面（+z）开放
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(0, -0.05f, -2.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(16.0f, 0.1f, 18.0f)),
                boxMesh, matFloor);
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(0, 4.0f, -11.05f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(16.0f, 8.0f, 0.1f)),
                boxMesh, matWall);
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(-8.05f, 4.0f, -2.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 8.0f, 18.0f)),
                boxMesh, matWall);
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(8.05f, 4.0f, -2.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 8.0f, 18.0f)),
                boxMesh, matWall);
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(0, 8.05f, -2.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(16.0f, 0.1f, 18.0f)),
                boxMesh, matCeiling);

    // --- 入口立柱：低角度夕阳穿过入口时投出体积光柱
    const float pillarX[4] = {-6.0f, -2.0f, 2.0f, 6.0f};
    for (float x : pillarX)
    {
        addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(x, 3.5f, 5.5f)) *
                        glm::scale(glm::mat4(1.0f), glm::vec3(0.35f, 7.0f, 0.35f)),
                    boxMesh, matWall);
    }

    // --- 三组展示：底座 + 金属球 + 灯板 + 灯球 + 聚光灯灯具（相机从室内朝夕阳方向看）
    // 左 -> 右：弱 / 强 / 极强，便于直观对比 HDR 高光（相机朝 +z 看，+x 在画面左侧）
    const float columns[3] = {4.0f, 0.0f, -4.0f};
    for (int i = 0; i < 3; ++i)
    {
        const float x = columns[i];
        addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.6f, 0.2f)) *
                        glm::scale(glm::mat4(1.0f), glm::vec3(1.4f, 1.2f, 1.4f)),
                    boxMesh, matPedestal);
        addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(x, 1.9f, 0.2f)) *
                        glm::scale(glm::mat4(1.0f), glm::vec3(0.85f)),
                    sphereMesh, matMetal);

        addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(x, 5.2f, 2.0f)) *
                        glm::scale(glm::mat4(1.0f), glm::vec3(2.2f, 1.4f, 0.06f)),
                    boxMesh, panelMaterial[i]);
        addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(x, 3.6f, -0.8f)) *
                        glm::scale(glm::mat4(1.0f), glm::vec3(0.3f)),
                    sphereMesh, ballMaterial[i]);
        addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(x, 6.8f, 0.6f)) *
                        glm::scale(glm::mat4(1.0f), glm::vec3(0.35f, 0.12f, 0.35f)),
                    boxMesh, spotMaterial[i]);
    }

    // --- 解析光源：灯板（矩形）、灯球、聚光灯
    const float innerCos = std::cos(12.0f * kPi / 180.0f);
    const float outerCos = std::cos(22.0f * kPi / 180.0f);
    for (int i = 0; i < 3; ++i)
    {
        const float x = columns[i];

        Light panel;
        panel.type = 0;
        panel.position = glm::vec3(x, 5.2f, 2.0f);
        panel.emission = panelEmissive[i];
        panel.u = glm::vec3(1.1f, 0.0f, 0.0f);
        panel.v = glm::vec3(0.0f, 0.7f, 0.0f);
        scene.lights.push_back(panel);

        Light ball;
        ball.type = 1;
        ball.position = glm::vec3(x, 3.6f, -0.8f);
        ball.emission = ballEmissive[i];
        ball.radius = 0.3f;
        scene.lights.push_back(ball);

        Light spot;
        spot.type = 2;
        spot.position = glm::vec3(x, 6.8f, 0.6f);
        spot.emission = spotIntensity[i];
        // 朝向相机一侧倾斜，让锥形光束在雾中呈前向散射
        spot.u = glm::normalize(glm::vec3(0.0f, -1.0f, -0.35f));
        spot.innerCos = innerCos;
        spot.outerCos = outerCos;
        scene.lights.push_back(spot);
    }

    scene.computeBounds();
    return scene;
}

// 阳光展厅：天窗百叶投下条纹光影 + 少量光源，低多边形且带体积雾，兼顾 HDR 与性能
Scene buildSunlitGalleryScene()
{
    Scene scene;

    auto addMaterial = [&](const glm::vec3& color, float roughness, float metallic = 0.0f,
                           float transmission = 0.0f) {
        const int index = static_cast<int>(scene.materials.size());
        scene.materials.push_back(makeMaterial(color, roughness, metallic, transmission));
        return index;
    };
    auto addEmissiveMaterial = [&](const glm::vec3& emission) {
        Material material = makeMaterial({0.0f, 0.0f, 0.0f}, 1.0f);
        material.emissive = emission;
        const int index = static_cast<int>(scene.materials.size());
        scene.materials.push_back(material);
        return index;
    };

    const int matFloor = addMaterial({0.16f, 0.15f, 0.14f}, 0.20f, 0.35f);
    const int matWall = addMaterial({0.24f, 0.23f, 0.21f}, 0.85f);
    const int matColumn = addMaterial({0.52f, 0.49f, 0.45f}, 0.55f);
    const int matBoard = addMaterial({0.02f, 0.02f, 0.02f}, 0.9f);
    const int matMetal = addMaterial({0.95f, 0.95f, 0.98f}, 0.08f, 1.0f);
    const int matCopper = addMaterial({0.95f, 0.55f, 0.32f}, 0.15f, 1.0f);
    const int matGlass = addMaterial({1.0f, 1.0f, 1.0f}, 0.0f, 0.0f, 1.0f);
    const int matStripWarm = addEmissiveMaterial({4.0f, 2.6f, 1.4f});
    const int matStripCool = addEmissiveMaterial({16.0f, 12.0f, 8.0f});
    const int matAccentCyan = addEmissiveMaterial({0.0f, 20.0f, 30.0f});
    const int matAccentMagenta = addEmissiveMaterial({30.0f, 4.0f, 20.0f});
    const int matSpotFixture = addEmissiveMaterial(glm::vec3(40.0f));
    // HDR 亮度参考条：0.5× / 1× / 2× / 5× / 10× 纸白（场景线性亮度 1.0 = 纸白）
    const float refLevels[5] = {0.5f, 1.0f, 2.0f, 5.0f, 10.0f};
    int matReference[5];
    for (int i = 0; i < 5; ++i)
    {
        matReference[i] = addEmissiveMaterial(glm::vec3(refLevels[i]));
    }

    // --- 网格
    const int boxMesh = static_cast<int>(scene.meshes.size());
    scene.meshes.push_back(makeBox());
    const int sphereMesh = static_cast<int>(scene.meshes.size());
    scene.meshes.push_back(makeSphere(48, 24));
    const int torusMesh = static_cast<int>(scene.meshes.size());
    scene.meshes.push_back(makeTorus(48, 24, 0.35f, 0.14f));
    for (auto& mesh : scene.meshes)
    {
        finalizeTangents(mesh);
    }

    auto addInstance = [&](const glm::mat4& transform, int meshIndex, int materialIndex) {
        Instance inst;
        inst.transform = transform;
        inst.mesh = meshIndex;
        inst.material = materialIndex;
        scene.instances.push_back(inst);
    };

    // --- 房间 14 x 7 x 18（z: -10 ~ +8），顶部为百叶、正面开放
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(0, -0.05f, -1.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(14.0f, 0.1f, 18.0f)),
                boxMesh, matFloor);
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(-7.05f, 3.5f, -1.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 7.0f, 18.0f)),
                boxMesh, matWall);
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(7.05f, 3.5f, -1.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 7.0f, 18.0f)),
                boxMesh, matWall);
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(0, 3.5f, -10.05f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(14.0f, 7.0f, 0.1f)),
                boxMesh, matWall);

    // --- 顶部百叶：低角度阳光穿过缝隙形成条纹光影
    for (float z = -8.8f; z <= 7.8f; z += 1.2f)
    {
        addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 6.9f, z)) *
                        glm::scale(glm::mat4(1.0f), glm::vec3(14.0f, 0.14f, 0.5f)),
                    boxMesh, matColumn);
    }

    // --- 柱廊与展示物
    const float columns[2] = {-5.8f, 5.8f};
    for (float x : columns)
    {
        addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(x, 3.45f, 3.0f)) *
                        glm::scale(glm::mat4(1.0f), glm::vec3(0.5f, 6.9f, 0.5f)),
                    boxMesh, matColumn);
    }

    const float displayX[3] = {-3.0f, 0.0f, 3.0f};
    for (float x : displayX)
    {
        addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.5f, 4.0f)) *
                        glm::scale(glm::mat4(1.0f), glm::vec3(1.2f, 1.0f, 1.2f)),
                    boxMesh, matColumn);
    }
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 1.7f, 4.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(0.7f)),
                sphereMesh, matMetal);
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.7f, 4.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(0.7f)),
                sphereMesh, matGlass);
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 1.7f, 4.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(0.7f)),
                sphereMesh, matCopper);

    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, 0.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(1.2f, 1.0f, 1.2f)),
                boxMesh, matColumn);
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.6f, 0.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(1.2f)),
                torusMesh, matMetal);

    // --- 发光灯带与彩色灯球（可见光源体）
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(-6.8f, 5.4f, -1.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(0.12f, 0.16f, 6.0f)),
                boxMesh, matStripWarm);
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(6.8f, 5.4f, -1.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(0.12f, 0.16f, 6.0f)),
                boxMesh, matStripCool);
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(-5.0f, 2.6f, 6.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(0.25f)),
                sphereMesh, matAccentCyan);
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 2.6f, 6.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(0.25f)),
                sphereMesh, matAccentMagenta);
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 6.6f, 2.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(0.3f, 0.1f, 0.3f)),
                boxMesh, matSpotFixture);
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 6.6f, 2.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(0.3f, 0.1f, 0.3f)),
                boxMesh, matSpotFixture);

    // 正对相机的悬挂亮度参考板：深色衬板 + 5 档发光方块（0.5/1/2/5/10 倍纸白）
    addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(3.9f, 2.05f, 5.6f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(4.2f, 1.5f, 0.08f)),
                boxMesh, matBoard);
    for (int i = 0; i < 5; ++i)
    {
        const float x = 2.3f + 0.8f * static_cast<float>(i);
        addInstance(glm::translate(glm::mat4(1.0f), glm::vec3(x, 2.05f, 5.53f)) *
                        glm::scale(glm::mat4(1.0f), glm::vec3(0.55f, 0.55f, 0.06f)),
                    boxMesh, matReference[i]);
    }

    // --- 解析光源
    Light warmStrip;
    warmStrip.type = 0;
    warmStrip.position = glm::vec3(-6.8f, 5.4f, -1.0f);
    warmStrip.emission = glm::vec3(4.0f, 2.6f, 1.4f);
    warmStrip.u = glm::vec3(0.0f, 0.0f, 3.0f);
    warmStrip.v = glm::vec3(0.0f, 0.35f, 0.0f);
    scene.lights.push_back(warmStrip);

    Light coolStrip;
    coolStrip.type = 0;
    coolStrip.position = glm::vec3(6.8f, 5.4f, -1.0f);
    coolStrip.emission = glm::vec3(16.0f, 12.0f, 8.0f);
    coolStrip.u = glm::vec3(0.0f, 0.0f, 3.0f);
    coolStrip.v = glm::vec3(0.0f, 0.35f, 0.0f);
    scene.lights.push_back(coolStrip);

    Light cyanBall;
    cyanBall.type = 1;
    cyanBall.position = glm::vec3(-5.0f, 2.6f, 6.0f);
    cyanBall.emission = glm::vec3(0.0f, 20.0f, 30.0f);
    cyanBall.radius = 0.25f;
    scene.lights.push_back(cyanBall);

    Light magentaBall;
    magentaBall.type = 1;
    magentaBall.position = glm::vec3(5.0f, 2.6f, 6.0f);
    magentaBall.emission = glm::vec3(30.0f, 4.0f, 20.0f);
    magentaBall.radius = 0.25f;
    scene.lights.push_back(magentaBall);

    const float innerCos = std::cos(10.0f * kPi / 180.0f);
    const float outerCos = std::cos(20.0f * kPi / 180.0f);
    const float spotX[2] = {-3.0f, 3.0f};
    const float spotPower[2] = {250.0f, 800.0f};
    for (int i = 0; i < 2; ++i)
    {
        Light spot;
        spot.type = 2;
        spot.position = glm::vec3(spotX[i], 6.6f, 2.0f);
        spot.emission = glm::vec3(spotPower[i], spotPower[i] * 0.95f, spotPower[i] * 0.88f);
        spot.u = glm::normalize(glm::vec3(0.0f, -1.0f, -0.45f));
        spot.innerCos = innerCos;
        spot.outerCos = outerCos;
        scene.lights.push_back(spot);
    }

    scene.computeBounds();
    return scene;
}
