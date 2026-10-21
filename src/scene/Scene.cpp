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

    scene.computeBounds();
    return scene;
}
