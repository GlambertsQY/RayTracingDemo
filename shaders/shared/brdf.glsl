#ifndef RT_BRDF_GLSL
#define RT_BRDF_GLSL

#include "shared/common.glsl"

// 采样基于给定法线的 GGX 微表面，返回出射方向与权重系数
bool sampleMetallicBrdf(vec3 n, vec3 v, vec3 f0, float roughness, out vec3 newDirection, out vec3 weight)
{
    const float nDotV = max(dot(n, v), 1e-3);
    const vec3 h = sampleGgxHalfVector(n, max(roughness, 0.02));
    const vec3 l = reflect(-v, h);

    const float nDotL = dot(n, l);
    if (nDotL <= 0.0)
    {
        return false;
    }

    const float nDotH = max(dot(n, h), 1e-3);
    const float vDotH = max(dot(v, h), 0.0);

    const float d = distributionGgx(nDotH, max(roughness, 0.02));
    const float g = geometrySchlickGgx(nDotV, roughness) * geometrySchlickGgx(nDotL, roughness);
    const vec3 f = fresnelSchlickVec(vDotH, f0);

    const float pdf = max(d * nDotH / (4.0 * max(vDotH, 1e-4)), 1e-6);
    const vec3 brdf = (d * g * f) / (4.0 * nDotV * nDotL);
    weight = brdf * nDotL / pdf;

    newDirection = l;
    return true;
}

#endif // RT_BRDF_GLSL
