#ifndef RT_LIGHTING_GLSL
#define RT_LIGHTING_GLSL

// 解析光源采样（依赖调用方先定义 traceOcclusion，且已包含 common.glsl / volumetric.glsl）
// 光源类型：0 = 矩形，1 = 球，2 = 聚光灯
//   position.w = 类型
//   emission.rgb = 辐射亮度（矩形/球）或发光强度（聚光灯）
//   u.xyz = 矩形半轴 u / 聚光灯方向；u.w = 聚光灯内锥 cos
//   v.xyz = 矩形半轴 v；v.w = 聚光灯外锥 cos

struct LightSample
{
    vec3  toLight;
    float distance;
    float distanceSquared;
    float geometryFactor;   // 面光源: 发光面余弦 * 面积；聚光灯: 锥形衰减
    bool  valid;
};

LightSample sampleLight(const GpuLight light, vec3 referencePos)
{
    LightSample ls;
    ls.valid = false;
    ls.distance = 0.0;
    ls.distanceSquared = 1.0;
    ls.geometryFactor = 0.0;
    ls.toLight = vec3(0.0, 1.0, 0.0);

    const int type = int(light.position.w + 0.5);

    vec3 point = light.position.xyz;
    vec3 normal = vec3(0.0, 1.0, 0.0);
    float area = 1.0;
    bool pointLike = false;

    if (type == 0)
    {
        point += light.u.xyz * (rand() * 2.0 - 1.0) + light.v.xyz * (rand() * 2.0 - 1.0);
        normal = normalize(cross(light.u.xyz, light.v.xyz));
        area = 4.0 * length(cross(light.u.xyz, light.v.xyz));
    }
    else if (type == 1)
    {
        const vec3 offset = randomUnitVector();
        point += offset * light.emission.w;
        normal = offset;
        area = 4.0 * kPi * light.emission.w * light.emission.w;
    }
    else
    {
        pointLike = true;
        normal = normalize(light.u.xyz);
    }

    vec3 toLight = point - referencePos;
    const float distanceSquared = max(dot(toLight, toLight), 1e-6);
    const float distance = sqrt(distanceSquared);
    ls.toLight = toLight / distance;
    ls.distance = distance;
    ls.distanceSquared = distanceSquared;

    if (pointLike)
    {
        const float cosAngle = dot(-ls.toLight, normal);
        const float inner = max(light.u.w, light.v.w);
        const float outer = min(light.u.w, light.v.w);
        const float cone = inner >= 0.9999 ? 1.0 : smoothstep(outer, inner, cosAngle);
        if (cone <= 0.0)
        {
            return ls;
        }
        ls.geometryFactor = cone;
    }
    else
    {
        const float lightCos =
            type == 0 ? abs(dot(normal, -ls.toLight)) : max(dot(normal, -ls.toLight), 0.0);
        if (lightCos <= 0.0)
        {
            return ls;
        }
        ls.geometryFactor = lightCos * area;
    }

    ls.valid = true;
    return ls;
}

// 表面点的直接光照（含雾透过率）
vec3 explicitLighting(vec3 hitPos, vec3 normal, vec3 diffuseAlbedo)
{
    vec3 result = vec3(0.0);
    if (!isFiniteVec(hitPos) || !isFiniteVec(normal) || dot(normal, normal) < 1e-8)
    {
        return result;
    }

    const vec3 sunDirection = normalize(fc.sunDir.xyz);
    const float sunCos = dot(normal, sunDirection);
    if (sunCos > 0.0 && fc.sunColor.w > 0.0)
    {
        if (!traceOcclusion(hitPos + normal * 0.005, sunDirection, 1e30))
        {
            result += diffuseAlbedo * kInvPi * fc.sunColor.rgb * fc.sunColor.w * sunCos *
                      fogTransmittance(fc.fogDistance);
        }
    }

    const int lightCount = int(fc.lightCount);
    if (lightCount > 0)
    {
        // 均匀随机选择一盏灯，乘以灯数作为 pdf 补偿；移动端避免每跳遍历所有灯
        const int lightIndex = min(int(rand() * float(lightCount)), lightCount - 1);
        const GpuLight light = gLights[lightIndex];
        const LightSample ls = sampleLight(light, hitPos);

        const float surfaceCos = dot(normal, ls.toLight);
        if (ls.valid && surfaceCos > 0.0 &&
            !traceOcclusion(hitPos + normal * 0.005, ls.toLight, max(ls.distance - 0.01, 0.001)))
        {
            result += diffuseAlbedo * kInvPi * light.emission.rgb * surfaceCos * ls.geometryFactor /
                      ls.distanceSquared * float(lightCount) * fogTransmittance(ls.distance);
        }
    }

    return result;
}

// 体积散射点的单次散射内光源采样（HG 相函数，无表面余弦项）
vec3 fogLighting(vec3 position, vec3 direction)
{
    vec3 result = vec3(0.0);
    const float g = clamp(fc.fogAnisotropy, -0.95, 0.95);

    const vec3 sunDirection = normalize(fc.sunDir.xyz);
    if (fc.sunColor.w > 0.0)
    {
        const float phase = hgPhase(dot(direction, sunDirection), g);
        if (!traceOcclusion(position, sunDirection, fc.fogDistance))
        {
            result += phase * fc.sunColor.rgb * fc.sunColor.w * fogTransmittance(fc.fogDistance);
        }
    }

    const int lightCount = int(fc.lightCount);
    if (lightCount > 0)
    {
        const int lightIndex = min(int(rand() * float(lightCount)), lightCount - 1);
        const GpuLight light = gLights[lightIndex];
        const LightSample ls = sampleLight(light, position);

        if (ls.valid && !traceOcclusion(position, ls.toLight, max(ls.distance - 0.01, 0.001)))
        {
            const float phase = hgPhase(dot(direction, ls.toLight), g);
            result += phase * light.emission.rgb * ls.geometryFactor / ls.distanceSquared *
                      float(lightCount) * fogTransmittance(ls.distance);
        }
    }

    return result;
}

#endif // RT_LIGHTING_GLSL
